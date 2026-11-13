#include "TestsPCH.h"

#include "Checkout.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Module/ModuleEvents.h"
#include "Shalltear/Network/HTTP.h"
#include "Shalltear/Network/Socket.h"
#include "Shalltear/Network/TLS.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <thread>

using namespace Shalltear;
using namespace Shalltear::Net;

#if !defined(SHALLTEAR_SHARED)
SLT_DECLARE_LINKED_PACKAGE(Network)
#endif

namespace {

	constexpr size_t BIG_SIZE = 1 << 20;

	template <typename Done>
	bool WaitFor(Done done, f64 seconds = 10.0) {
		const auto until = std::chrono::steady_clock::now() + std::chrono::duration<f64>(seconds);
		while (!done()) {
			if (std::chrono::steady_clock::now() > until) {
				return false;
			}
			Net::Dispatch();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return true;
	}

	char BigByte(size_t i) {
		return static_cast<char>('a' + (i * 7 + i / 1000) % 26);
	}

	class TestServer {
	public:
		explicit TestServer(std::shared_ptr<TLSIdentity> identity = nullptr) : _identity(std::move(identity)) {
			str error;
			_listener = TCPListener::Listen(Address::Loopback(0), &error);
			EXPECT_NE(_listener, nullptr) << error;
			if (_listener) {
				_thread = std::thread([this] { Loop(); });
			}
		}

		~TestServer() {
			_stop = true;
			if (_thread.joinable()) {
				_thread.join();
			}
		}

		str URL(std::string_view path) const {
			return std::format("{}://127.0.0.1:{}{}", _identity ? "https" : "http", _listener ? _listener->GetPort() : 0, path);
		}

		std::atomic<u32> served{0};
	private:
		struct Stream {
			std::unique_ptr<TCPSocket> tcp;
			std::unique_ptr<TLSStream> tls;

			size_t Send(std::span<const u8> data) { return tls ? tls->Send(data) : tcp->Send(data); }
			size_t Receive(std::span<u8> out) { return tls ? tls->Receive(out) : tcp->Receive(out); }
			SocketState State() const { return tls ? tls->GetState() : tcp->GetState(); }
		};

		void Loop() {
			while (!_stop) {
				std::unique_ptr<TCPSocket> socket = _listener->Accept();
				if (!socket) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				Stream stream;
				if (_identity) {
					stream.tls = TLSStream::Accept(std::move(socket), _identity);
				} else {
					stream.tcp = std::move(socket);
				}
				Serve(stream);
			}
		}

		bool Write(Stream& stream, std::string_view text) {
			size_t at = 0;
			while (at < text.size() && !_stop) {
				const size_t sent = stream.Send({reinterpret_cast<const u8*>(text.data()) + at, text.size() - at});
				if (sent == 0) {
					if (stream.State() != SocketState::Open) {
						return false;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				at += sent;
			}
			return at == text.size();
		}

		void Respond(Stream& stream, std::string_view status, std::string_view headers, std::string_view body, bool withLength = true) {
			str head = std::format("HTTP/1.1 {}\r\nConnection: close\r\n{}", status, headers);
			if (withLength) {
				head += std::format("Content-Length: {}\r\n", body.size());
			}
			head += "\r\n";
			Write(stream, head);
			Write(stream, body);
		}

		void Serve(Stream& stream) {
			str request;
			u8 buffer[8192];
			size_t headerEnd = str::npos;
			size_t bodySize = 0;
			const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!_stop && std::chrono::steady_clock::now() < until) {
				const size_t read = stream.Receive(buffer);
				request.append(reinterpret_cast<const char*>(buffer), read);
				if (headerEnd == str::npos && (headerEnd = request.find("\r\n\r\n")) != str::npos) {
					const size_t length = request.find("Content-Length: ");
					if (length != str::npos && length < headerEnd) {
						bodySize = std::strtoull(request.c_str() + length + 16, nullptr, 10);
					}
				}
				if (headerEnd != str::npos && request.size() >= headerEnd + 4 + bodySize) {
					break;
				}
				if (read == 0) {
					if (stream.State() != SocketState::Open && stream.State() != SocketState::Connecting) {
						return;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
			if (headerEnd == str::npos) {
				return;
			}
			++served;
			const str method = request.substr(0, request.find(' '));
			const size_t pathStart = method.size() + 1;
			const str path = request.substr(pathStart, request.find(' ', pathStart) - pathStart);
			const str body = request.substr(headerEnd + 4, bodySize);
			str echo;
			if (const size_t at = request.find("X-Echo: "); at != str::npos && at < headerEnd) {
				echo = request.substr(at + 8, request.find("\r\n", at) - at - 8);
			}

			if (path == "/hello") {
				Respond(stream, "200 OK", "Content-Type: text/plain\r\nX-Test: yes\r\n", method == "HEAD" ? "" : "hello");
			} else if (path == "/echo") {
				Respond(stream, "200 OK", std::format("X-Method: {}\r\nX-Echo: {}\r\n", method, echo), body);
			} else if (path == "/redirect") {
				Respond(stream, "302 Found", "Location: /hello\r\n", "");
			} else if (path == "/chain") {
				Respond(stream, "302 Found", "Location: /redirect\r\n", "");
			} else if (path == "/chunked") {
				Write(stream, "HTTP/1.1 200 OK\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");
			} else if (path == "/big") {
				str big(BIG_SIZE, '\0');
				for (size_t i = 0; i < big.size(); ++i) {
					big[i] = BigByte(i);
				}
				Respond(stream, "200 OK", "", big);
			} else if (path == "/slow") {
				Write(stream, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 5\r\n\r\n");
				for (int i = 0; i < 300 && !_stop; ++i) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				Write(stream, "late!");
			} else {
				Respond(stream, "404 Not Found", "", "missing");
			}
			if (stream.tls) {
				stream.tls->Close();
			} else {
				stream.tcp->Close();
			}
		}

		std::shared_ptr<TLSIdentity> _identity;
		std::unique_ptr<TCPListener> _listener;
		std::thread _thread;
		std::atomic<bool> _stop{false};
	};

	HTTP::Response Fetch(HTTP::Request request, HTTP::ProgressCallback progress = {}, f64 seconds = 15.0) {
		HTTP::Response result;
		bool done = false;
		HTTP::Send(
			std::move(request),
			[&](const HTTP::Response& response) {
				result = response;
				done = true;
			},
			std::move(progress));
		EXPECT_TRUE(WaitFor([&] { return done; }, seconds));
		return result;
	}

	HTTP::Request Get(str url) {
		HTTP::Request request;
		request.url = std::move(url);
		return request;
	}

	str Text(const Vector<u8>& bytes) {
		return str(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	class NetworkHTTPFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			if (!HTTP::IsAvailable()) {
				GTEST_SKIP() << "no HTTP backend on this system";
			}
		}
	};

} // anonymous namespace

TEST_F(NetworkHTTPFixture, GetReturnsStatusBodyAndHeaders) {
	TestServer server;
	const HTTP::Response response = Fetch(Get(server.URL("/hello")));
	ASSERT_EQ(response.result, HTTP::Result::Ok) << response.error;
	EXPECT_EQ(response.status, 200u);
	EXPECT_EQ(Text(response.body), "hello");
	EXPECT_EQ(response.FindHeader("x-test"), "yes");
	EXPECT_EQ(response.FindHeader("CONTENT-TYPE"), "text/plain");
	EXPECT_EQ(response.url, server.URL("/hello"));
}

TEST_F(NetworkHTTPFixture, MethodsHeadersAndBodiesReachServer) {
	TestServer server;
	for (HTTP::Method method : {HTTP::Method::Post, HTTP::Method::Put, HTTP::Method::Patch, HTTP::Method::Delete}) {
		HTTP::Request request = Get(server.URL("/echo"));
		request.method = method;
		request.headers.push_back({"X-Echo", "abc 123"});
		const str payload = std::format("payload for {}", HTTP::GetMethodName(method));
		request.body.assign(payload.begin(), payload.end());
		const HTTP::Response response = Fetch(std::move(request));
		ASSERT_EQ(response.result, HTTP::Result::Ok) << response.error;
		EXPECT_EQ(response.FindHeader("X-Method"), HTTP::GetMethodName(method));
		EXPECT_EQ(response.FindHeader("X-Echo"), "abc 123");
		EXPECT_EQ(Text(response.body), payload);
	}
	HTTP::Request head = Get(server.URL("/hello"));
	head.method = HTTP::Method::Head;
	const HTTP::Response response = Fetch(std::move(head));
	ASSERT_EQ(response.result, HTTP::Result::Ok) << response.error;
	EXPECT_EQ(response.status, 200u);
	EXPECT_TRUE(response.body.empty());
}

TEST_F(NetworkHTTPFixture, RedirectsFollowedWithinLimit) {
	TestServer server;
	const HTTP::Response followed = Fetch(Get(server.URL("/chain")));
	ASSERT_EQ(followed.result, HTTP::Result::Ok) << followed.error;
	EXPECT_EQ(followed.status, 200u);
	EXPECT_EQ(Text(followed.body), "hello");
	EXPECT_EQ(followed.url, server.URL("/hello"));

	HTTP::Request kept = Get(server.URL("/redirect"));
	kept.maxRedirects = 0;
	const HTTP::Response returned = Fetch(std::move(kept));
	ASSERT_EQ(returned.result, HTTP::Result::Ok) << returned.error;
	EXPECT_EQ(returned.status, 302u);
	EXPECT_EQ(returned.FindHeader("Location"), "/hello");

	HTTP::Request limited = Get(server.URL("/chain"));
	limited.maxRedirects = 1;
	EXPECT_EQ(Fetch(std::move(limited)).result, HTTP::Result::Failed);
}

TEST_F(NetworkHTTPFixture, ErrorStatusesAndChunkedBodiesArrive) {
	TestServer server;
	const HTTP::Response missing = Fetch(Get(server.URL("/nothing")));
	ASSERT_EQ(missing.result, HTTP::Result::Ok) << missing.error;
	EXPECT_EQ(missing.status, 404u);
	EXPECT_EQ(Text(missing.body), "missing");

	const HTTP::Response chunked = Fetch(Get(server.URL("/chunked")));
	ASSERT_EQ(chunked.result, HTTP::Result::Ok) << chunked.error;
	EXPECT_EQ(Text(chunked.body), "hello world");
}

TEST_F(NetworkHTTPFixture, TimeoutAndCancelEndSlowRequests) {
	TestServer server;
	HTTP::Request slow = Get(server.URL("/slow"));
	slow.timeoutSeconds = 0.5f;
	const auto started = std::chrono::steady_clock::now();
	const HTTP::Response timedOut = Fetch(std::move(slow));
	EXPECT_EQ(timedOut.result, HTTP::Result::TimedOut) << timedOut.error;
	EXPECT_LT(std::chrono::duration<f64>(std::chrono::steady_clock::now() - started).count(), 2.5);

	TestServer second;
	HTTP::Response cancelled;
	bool done = false;
	const HTTP::RequestId id = HTTP::Send(Get(second.URL("/slow")), [&](const HTTP::Response& response) {
		cancelled = response;
		done = true;
	});
	WaitFor([&] { return second.served > 0; });
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	const auto cancelledAt = std::chrono::steady_clock::now();
	HTTP::Cancel(id);
	ASSERT_TRUE(WaitFor([&] { return done; }));
	EXPECT_EQ(cancelled.result, HTTP::Result::Cancelled) << cancelled.error;
	EXPECT_LT(std::chrono::duration<f64>(std::chrono::steady_clock::now() - cancelledAt).count(), 1.5);
	HTTP::Cancel(id);
}

#if !defined(SHALLTEAR_SHARED)

TEST_F(NetworkHTTPFixture, ImageUnloadDropsPendingCallbacks) {
	const char owner = 0;
	SLT_CALL_LINKED_PACKAGE(Network, &owner);
	TestServer server;
	bool done = false;
	HTTP::Send(Get(server.URL("/slow")), [&done](const HTTP::Response&) { done = true; });
	ASSERT_TRUE(WaitFor([&] { return server.served > 0; }));
	const char image = 0;
	ModuleEvents::ImageUnloading.Publish(EventBus::GetGlobal(), ImageUnloadingEvent{&image});
	WaitFor([] { return false; }, 1.0);
	FrameStages::UnregisterOwnedBy(&owner);
	EXPECT_FALSE(done);
	EXPECT_EQ(Fetch(Get(server.URL("/hello"))).result, HTTP::Result::Ok);
}

#endif

TEST_F(NetworkHTTPFixture, DownloadStreamsToFileWithProgress) {
	TestServer server;
	const std::filesystem::path folder =
		std::filesystem::temp_directory_path() / std::format("shalltear_http_{}", std::chrono::steady_clock::now().time_since_epoch().count());
	std::filesystem::create_directories(folder);
	const std::filesystem::path target = folder / "big.bin";

	HTTP::Request request = Get(server.URL("/big"));
	request.downloadPath = target.generic_string();
	u64 lastReceived = 0;
	u64 lastTotal = 0;
	bool monotonic = true;
	u32 calls = 0;
	const HTTP::Response response = Fetch(std::move(request), [&](u64 received, u64 total) {
		monotonic = monotonic && received >= lastReceived;
		lastReceived = received;
		lastTotal = total;
		++calls;
	});
	ASSERT_EQ(response.result, HTTP::Result::Ok) << response.error;
	EXPECT_TRUE(response.body.empty());
	EXPECT_GT(calls, 0u);
	EXPECT_TRUE(monotonic);
	EXPECT_EQ(lastReceived, BIG_SIZE);
	EXPECT_EQ(lastTotal, BIG_SIZE);
	std::ifstream file(target, std::ios::binary);
	const str written{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
	file.close();
	ASSERT_EQ(written.size(), BIG_SIZE);
	size_t wrong = 0;
	for (size_t i = 0; i < written.size(); ++i) {
		wrong += written[i] != BigByte(i) ? 1u : 0u;
	}
	EXPECT_EQ(wrong, 0u);
	EXPECT_FALSE(std::filesystem::exists(folder / "big.bin.download"));

	HTTP::Request missing = Get(server.URL("/nothing"));
	missing.downloadPath = (folder / "missing.bin").generic_string();
	const HTTP::Response refused = Fetch(std::move(missing));
	EXPECT_EQ(refused.status, 404u);
	EXPECT_FALSE(std::filesystem::exists(folder / "missing.bin"));
	EXPECT_FALSE(std::filesystem::exists(folder / "missing.bin.download"));
	std::error_code ec;
	std::filesystem::remove_all(folder, ec);
}

TEST_F(NetworkHTTPFixture, UnreachableAndUntrustedServersFail) {
	str error;
	std::unique_ptr<TCPListener> closed = TCPListener::Listen(Address::Loopback(0), &error);
	ASSERT_NE(closed, nullptr) << error;
	const u16 port = closed->GetPort();
	closed.reset();
	const HTTP::Response refused = Fetch(Get(std::format("http://127.0.0.1:{}/hello", port)));
	EXPECT_EQ(refused.result, HTTP::Result::Failed);
	EXPECT_FALSE(refused.error.empty());
	EXPECT_EQ(Fetch(Get("ftp://127.0.0.1/file")).result, HTTP::Result::Failed);

	if (!TLSStream::IsAvailable()) {
		return;
	}
	const std::filesystem::path file = Checkout::Find("Source/Tests/Fixtures/Network/localhost.p12");
	std::ifstream stream(file, std::ios::binary);
	const std::vector<u8> pfx{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
	std::shared_ptr<TLSIdentity> identity = TLSIdentity::FromPKCS12(pfx, "shalltear", &error);
	ASSERT_NE(identity, nullptr) << error;
	TestServer secure(identity);
	const HTTP::Response untrusted = Fetch(Get(secure.URL("/hello")));
	EXPECT_EQ(untrusted.result, HTTP::Result::Failed);
	EXPECT_EQ(untrusted.status, 0u);
	EXPECT_FALSE(untrusted.error.empty());
}

TEST_F(NetworkHTTPFixture, PublicHTTPSWhenOnlineTestsAreAsked) {
	if (!std::getenv("SHALLTEAR_TEST_ONLINE")) {
		GTEST_SKIP() << "set SHALLTEAR_TEST_ONLINE to reach a public host";
	}
	const HTTP::Response response = Fetch(Get("https://example.com/"));
	ASSERT_EQ(response.result, HTTP::Result::Ok) << response.error;
	EXPECT_EQ(response.status, 200u);
	EXPECT_NE(Text(response.body).find("Example Domain"), str::npos);
}
