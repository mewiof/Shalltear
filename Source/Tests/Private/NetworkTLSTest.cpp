#include "TestsPCH.h"

#include "Checkout.h"

#include "Shalltear/Network/Socket.h"
#include "Shalltear/Network/TLS.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

using namespace Shalltear;
using namespace Shalltear::Net;

namespace {

	constexpr std::string_view IdentityHash = "2be6c3fd62f6b8a5be59a15d2e6035bdec4222f8dce11bd273704beaba323cc5";

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

	std::vector<u8> ReadFixture(const char* relative) {
		const std::filesystem::path file = Checkout::Find(relative);
		std::ifstream stream(file, std::ios::binary);
		return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
	}

	class NetworkTLSFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			if (!TLSStream::IsAvailable()) {
				GTEST_SKIP() << "no TLS library on this system";
			}
			const std::vector<u8> pfx = ReadFixture("Source/Tests/Fixtures/Network/localhost.p12");
			ASSERT_FALSE(pfx.empty());
			str error;
			identity = TLSIdentity::FromPKCS12(pfx, "shalltear", &error);
			ASSERT_NE(identity, nullptr) << error;
			listener = TCPListener::Listen(Address::Loopback(0), &error);
			ASSERT_NE(listener, nullptr) << error;
		}

		void Open(TLSOptions options) {
			client = TLSStream::Connect("127.0.0.1", listener->GetPort(), std::move(options));
			WaitFor([&] {
				if (!server) {
					if (std::unique_ptr<TCPSocket> socket = listener->Accept()) {
						server = TLSStream::Accept(std::move(socket), identity);
					}
				}
				const SocketState clientState = client->GetState();
				const SocketState serverState = server ? server->GetState() : SocketState::Connecting;
				return clientState != SocketState::Connecting && (serverState != SocketState::Connecting || clientState == SocketState::Failed);
			});
		}

		static TLSOptions Pinned(const CertificateHash& hash) {
			TLSOptions options;
			options.serverName = "localhost";
			options.pinnedCertificate = hash;
			return options;
		}

		str Exchange(TLSStream& from, TLSStream& to, const str& payload) {
			size_t sent = 0;
			str got;
			u8 buffer[64 * 1024];
			WaitFor([&] {
				for (size_t taken = 1; taken > 0 && sent < payload.size();) {
					taken = from.Send({reinterpret_cast<const u8*>(payload.data()) + sent, std::min<size_t>(payload.size() - sent, 64 * 1024)});
					sent += taken;
				}
				for (size_t read = to.Receive(buffer); read > 0; read = to.Receive(buffer)) {
					got.append(reinterpret_cast<const char*>(buffer), read);
				}
				return got.size() >= payload.size() || to.GetState() != SocketState::Open;
			});
			return got;
		}

		std::shared_ptr<TLSIdentity> identity;
		std::unique_ptr<TCPListener> listener;
		std::unique_ptr<TLSStream> client;
		std::unique_ptr<TLSStream> server;
	};

} // anonymous namespace

TEST(NetworkTLSHashTest, ParsesHexWithSeparators) {
	CertificateHash hash{};
	ASSERT_TRUE(ParseCertificateHash(IdentityHash, hash));
	EXPECT_EQ(hash[0], 0x2B);
	EXPECT_EQ(hash[31], 0xC5);
	CertificateHash spaced{};
	ASSERT_TRUE(ParseCertificateHash("2B:E6:C3:FD:62:F6:B8:A5 BE:59:A1:5D:2E:60:35:BD:EC:42:22:F8:DC:E1:1B:D2:73:70:4B:EA:BA:32:3C:C5",
									 spaced));
	EXPECT_EQ(spaced, hash);
	EXPECT_FALSE(ParseCertificateHash("2be6", hash));
	EXPECT_FALSE(ParseCertificateHash(str(IdentityHash) + "00", hash));
	EXPECT_FALSE(ParseCertificateHash(str(IdentityHash.substr(0, 63)) + "g", hash));
}

TEST_F(NetworkTLSFixture, PinnedLoopbackCarriesBothWays) {
	CertificateHash expected{};
	ASSERT_TRUE(ParseCertificateHash(IdentityHash, expected));
	EXPECT_EQ(identity->GetCertificateHash(), expected);

	Open(Pinned(expected));
	ASSERT_NE(server, nullptr);
	ASSERT_EQ(client->GetState(), SocketState::Open) << client->GetError();
	ASSERT_EQ(server->GetState(), SocketState::Open) << server->GetError();
	CertificateHash presented{};
	ASSERT_TRUE(client->GetPeerCertificateHash(presented));
	EXPECT_EQ(presented, expected);

	EXPECT_EQ(Exchange(*client, *server, "hello server"), "hello server");
	EXPECT_EQ(Exchange(*server, *client, "hello client"), "hello client");
	str large(3 * 1024 * 1024 + 17, '\0');
	for (size_t i = 0; i < large.size(); ++i) {
		large[i] = static_cast<char>((i * 31 + i / 4096) & 0xFF);
	}
	EXPECT_EQ(Exchange(*client, *server, large), large);

	client->Close();
	u8 buffer[16];
	EXPECT_TRUE(WaitFor([&] {
		server->Receive(buffer);
		return server->GetState() == SocketState::Closed;
	}));
	EXPECT_EQ(client->Send({buffer, 4}), 0u);
}

TEST_F(NetworkTLSFixture, SystemValidationRefusesSelfSigned) {
	TLSOptions options;
	options.serverName = "localhost";
	Open(options);
	EXPECT_EQ(client->GetState(), SocketState::Failed);
	EXPECT_NE(client->GetError().find("certificate"), str::npos) << client->GetError();
}

TEST_F(NetworkTLSFixture, WrongPinFails) {
	CertificateHash other{};
	other.fill(0xAB);
	Open(Pinned(other));
	EXPECT_EQ(client->GetState(), SocketState::Failed);
	EXPECT_NE(client->GetError().find("pinned"), str::npos) << client->GetError();
}

TEST_F(NetworkTLSFixture, CallbacksReportOpenedAndReadable) {
	Open(Pinned(identity->GetCertificateHash()));
	ASSERT_NE(server, nullptr);
	ASSERT_EQ(server->GetState(), SocketState::Open) << server->GetError();
	std::vector<SocketEvent> events;
	str received;
	server->SetCallback([&](SocketEvent event) {
		events.push_back(event);
		u8 buffer[64];
		const size_t read = server->Receive(buffer);
		received.append(reinterpret_cast<const char*>(buffer), read);
	});
	const str message = "through callbacks";
	EXPECT_EQ(client->Send({reinterpret_cast<const u8*>(message.data()), message.size()}), message.size());
	ASSERT_TRUE(WaitFor([&] {
		client->GetState();
		return received == message;
	}));
	EXPECT_EQ(events.front(), SocketEvent::Opened);
	EXPECT_NE(std::find(events.begin(), events.end(), SocketEvent::Readable), events.end());
}

TEST(NetworkTLSIdentityTest, WrongPasswordRefused) {
	if (!TLSStream::IsAvailable()) {
		GTEST_SKIP() << "no TLS library on this system";
	}
	const std::vector<u8> pfx = ReadFixture("Source/Tests/Fixtures/Network/localhost.p12");
	ASSERT_FALSE(pfx.empty());
	str error;
	EXPECT_EQ(TLSIdentity::FromPKCS12(pfx, "wrong", &error), nullptr);
	EXPECT_FALSE(error.empty());
	const u8 garbage[] = {1, 2, 3, 4};
	EXPECT_EQ(TLSIdentity::FromPKCS12(garbage, "shalltear", &error), nullptr);
}
