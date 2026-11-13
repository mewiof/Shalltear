#include "TestsPCH.h"

#include "Shalltear/Network/Address.h"
#include "Shalltear/Network/Socket.h"

#include <chrono>
#include <thread>

using namespace Shalltear;
using namespace Shalltear::Net;

namespace {

	template <typename Done>
	bool WaitFor(Done done, f64 seconds = 5.0) {
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

	std::span<const u8> Bytes(std::string_view text) {
		return {reinterpret_cast<const u8*>(text.data()), text.size()};
	}

	str ReadAll(TCPSocket& socket, size_t expected) {
		str got;
		u8 buffer[4096];
		WaitFor([&] {
			const size_t read = socket.Receive(buffer);
			got.append(reinterpret_cast<const char*>(buffer), read);
			return got.size() >= expected || socket.GetState() != SocketState::Open;
		});
		return got;
	}

	struct Pair {
		std::unique_ptr<TCPListener> listener;
		std::unique_ptr<TCPSocket> client;
		std::unique_ptr<TCPSocket> server;
	};

	Pair Connect(std::string_view host) {
		Pair pair;
		str error;
		pair.listener = TCPListener::Listen(Address::Loopback(0), &error);
		EXPECT_NE(pair.listener, nullptr) << error;
		if (!pair.listener) {
			return pair;
		}
		pair.client = TCPSocket::Connect(host, pair.listener->GetPort());
		WaitFor([&] {
			const bool connected = pair.client->GetState() != SocketState::Connecting;
			if (!pair.server) {
				pair.server = pair.listener->Accept();
			}
			return pair.server && connected;
		});
		return pair;
	}

} // anonymous namespace

TEST(NetworkAddressTest, ParsesAndFormatsBothFamilies) {
	Address address;
	ASSERT_TRUE(Address::Parse("127.0.0.1:8080", address));
	EXPECT_FALSE(address.v6);
	EXPECT_EQ(address.port, 8080);
	EXPECT_EQ(address, Address::Loopback(8080));
	EXPECT_EQ(address.ToString(), "127.0.0.1:8080");

	ASSERT_TRUE(Address::Parse("[::1]:443", address));
	EXPECT_TRUE(address.v6);
	EXPECT_EQ(address, Address::Loopback(443, true));
	EXPECT_EQ(address.ToString(), "[::1]:443");

	ASSERT_TRUE(Address::Parse("10.1.2.3", address));
	EXPECT_EQ(address.port, 0);
	ASSERT_TRUE(Address::Parse("fe80::1", address));
	EXPECT_TRUE(address.v6);

	for (std::string_view bad : {"1.2.3:4", "[::1", "[::1]x", "host:80", ":80", "1.2.3.4:70000", "1.2.3.4:", ""}) {
		EXPECT_FALSE(Address::Parse(bad, address)) << bad;
	}
}

TEST(NetworkSocketTest, TCPLoopbackCarriesBothWaysThenCloses) {
	Pair pair = Connect("127.0.0.1");
	ASSERT_NE(pair.server, nullptr);
	ASSERT_EQ(pair.client->GetState(), SocketState::Open) << pair.client->GetError();
	EXPECT_EQ(pair.client->GetPeerAddress(), Address::Loopback(pair.listener->GetPort()));

	EXPECT_EQ(pair.client->Send(Bytes("ping")), 4u);
	EXPECT_EQ(ReadAll(*pair.server, 4), "ping");
	EXPECT_EQ(pair.server->Send(Bytes("pong!")), 5u);
	EXPECT_EQ(ReadAll(*pair.client, 5), "pong!");

	pair.server->Close();
	EXPECT_EQ(pair.server->GetState(), SocketState::Closed);
	u8 buffer[16];
	EXPECT_TRUE(WaitFor([&] {
		pair.client->Receive(buffer);
		return pair.client->GetState() == SocketState::Closed;
	}));
	EXPECT_EQ(pair.client->Send(Bytes("late")), 0u);
}

TEST(NetworkSocketTest, HostNameResolvesBeforeConnecting) {
	Pair pair = Connect("localhost");
	ASSERT_NE(pair.server, nullptr);
	EXPECT_EQ(pair.client->GetState(), SocketState::Open) << pair.client->GetError();
}

TEST(NetworkSocketTest, RefusedAndUnknownHostsFail) {
	str error;
	std::unique_ptr<TCPListener> closed = TCPListener::Listen(Address::Loopback(0), &error);
	ASSERT_NE(closed, nullptr) << error;
	const u16 port = closed->GetPort();
	closed.reset();

	std::unique_ptr<TCPSocket> refused = TCPSocket::Connect(Address::Loopback(port));
	ASSERT_TRUE(WaitFor([&] { return refused->GetState() != SocketState::Connecting; }, 15.0));
	EXPECT_EQ(refused->GetState(), SocketState::Failed);
	EXPECT_FALSE(refused->GetError().empty());

	std::unique_ptr<TCPSocket> unknown = TCPSocket::Connect("bad..host", 80);
	ASSERT_TRUE(WaitFor([&] { return unknown->GetState() != SocketState::Connecting; }, 15.0));
	EXPECT_EQ(unknown->GetState(), SocketState::Failed);
	EXPECT_NE(unknown->GetError().find("bad..host"), str::npos) << unknown->GetError();
}

TEST(NetworkSocketTest, CallbacksArriveFromDispatch) {
	str error;
	std::unique_ptr<TCPListener> listener = TCPListener::Listen(Address::Loopback(0), &error);
	ASSERT_NE(listener, nullptr) << error;
	std::unique_ptr<TCPSocket> accepted;
	listener->SetCallback([&](SocketEvent event) {
		if (event == SocketEvent::Readable && !accepted) {
			accepted = listener->Accept();
		}
	});

	std::unique_ptr<TCPSocket> client = TCPSocket::Connect("127.0.0.1", listener->GetPort());
	std::vector<SocketEvent> events;
	str received;
	client->SetCallback([&](SocketEvent event) {
		events.push_back(event);
		if (event == SocketEvent::Readable) {
			u8 buffer[64];
			const size_t read = client->Receive(buffer);
			received.append(reinterpret_cast<const char*>(buffer), read);
		}
	});
	ASSERT_TRUE(WaitFor([&] { return accepted != nullptr && !events.empty(); }));
	EXPECT_EQ(events.front(), SocketEvent::Opened);

	EXPECT_EQ(accepted->Send(Bytes("hello")), 5u);
	ASSERT_TRUE(WaitFor([&] { return received == "hello"; }));
	accepted.reset();
	ASSERT_TRUE(WaitFor([&] { return !events.empty() && events.back() == SocketEvent::Closed; }));
	EXPECT_EQ(std::count(events.begin(), events.end(), SocketEvent::Opened), 1);
	EXPECT_EQ(std::count(events.begin(), events.end(), SocketEvent::Closed), 1);
}

TEST(NetworkSocketTest, UDPDatagramsKeepBoundaries) {
	str error;
	std::unique_ptr<UDPSocket> a = UDPSocket::Bind(Address::Loopback(0), &error);
	ASSERT_NE(a, nullptr) << error;
	std::unique_ptr<UDPSocket> b = UDPSocket::Bind(Address::Loopback(0), &error);
	ASSERT_NE(b, nullptr) << error;
	EXPECT_NE(a->GetPort(), b->GetPort());

	EXPECT_TRUE(a->SendTo(Address::Loopback(b->GetPort()), Bytes("first")));
	EXPECT_TRUE(a->SendTo(Address::Loopback(b->GetPort()), Bytes("second datagram")));
	u8 buffer[64];
	Address from;
	size_t read = 0;
	ASSERT_TRUE(WaitFor([&] { return (read = b->ReceiveFrom(buffer, from)) > 0; }));
	EXPECT_EQ(str(reinterpret_cast<const char*>(buffer), read), "first");
	EXPECT_EQ(from, Address::Loopback(a->GetPort()));
	ASSERT_TRUE(WaitFor([&] { return (read = b->ReceiveFrom(std::span<u8>(buffer, 6), from)) > 0; }));
	EXPECT_EQ(str(reinterpret_cast<const char*>(buffer), read), "second");
	EXPECT_EQ(b->ReceiveFrom(buffer, from), 0u);

	bool readable = false;
	b->SetCallback([&](SocketEvent event) { readable = readable || event == SocketEvent::Readable; });
	EXPECT_TRUE(a->SendTo(Address::Loopback(b->GetPort()), Bytes("third")));
	EXPECT_TRUE(WaitFor([&] { return readable; }));
}

TEST(NetworkSocketTest, DualStackUDPReportsPlainIPv4Senders) {
	std::unique_ptr<UDPSocket> both = UDPSocket::Bind(Address::Any(0, true));
	if (!both) {
		GTEST_SKIP() << "no IPv6 on this machine";
	}
	std::unique_ptr<UDPSocket> v4 = UDPSocket::Bind(Address::Loopback(0));
	ASSERT_NE(v4, nullptr);
	EXPECT_TRUE(v4->SendTo(Address::Loopback(both->GetPort()), Bytes("mapped")));
	u8 buffer[16];
	Address from;
	size_t read = 0;
	ASSERT_TRUE(WaitFor([&] { return (read = both->ReceiveFrom(buffer, from)) > 0; }));
	EXPECT_FALSE(from.v6);
	EXPECT_EQ(from, Address::Loopback(v4->GetPort()));
	EXPECT_TRUE(both->SendTo(from, Bytes("back")));
	ASSERT_TRUE(WaitFor([&] { return (read = v4->ReceiveFrom(buffer, from)) > 0; }));
	EXPECT_EQ(str(reinterpret_cast<const char*>(buffer), read), "back");
}
