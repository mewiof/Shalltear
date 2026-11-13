#pragma once

#include "Shalltear/Core/Types.h"
#include "Shalltear/Network/NetworkAPI.h"
#include "Shalltear/Network/Socket.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace Shalltear::Net {

	// SHA-256 of certificate's DER bytes
	using CertificateHash = std::array<u8, 32>;

	// hex, colons/spaces allowed
	SHALLTEAR_NETWORK_API bool ParseCertificateHash(std::string_view hex, CertificateHash& out);

	struct TLSOptions {
		// SNI/validated name, host when empty
		str serverName;
		// leaf must match (system validation skipped)
		std::optional<CertificateHash> pinnedCertificate;
	};

	struct TLSIdentityData;

	class SHALLTEAR_NETWORK_API TLSIdentity {
	public:
		explicit TLSIdentity(std::unique_ptr<TLSIdentityData> data);
		~TLSIdentity();
		TLSIdentity(const TLSIdentity&) = delete;
		TLSIdentity& operator=(const TLSIdentity&) = delete;

		static std::shared_ptr<TLSIdentity> FromPKCS12(std::span<const u8> pfx, std::string_view password, str* outError = nullptr);

		const CertificateHash& GetCertificateHash() const;
		TLSIdentityData& GetData() const { return *_data; }
	private:
		std::unique_ptr<TLSIdentityData> _data;
	};

	struct TLSCore;

	class SHALLTEAR_NETWORK_API TLSStream {
	public:
		explicit TLSStream(std::shared_ptr<TLSCore> core);
		~TLSStream();
		TLSStream(const TLSStream&) = delete;
		TLSStream& operator=(const TLSStream&) = delete;

		static std::unique_ptr<TLSStream> Connect(std::string_view host, u16 port, TLSOptions options = {});
		static std::unique_ptr<TLSStream> Accept(std::unique_ptr<TCPSocket> socket, std::shared_ptr<TLSIdentity> identity);
		static bool IsAvailable();

		// Connecting until handshake/validation finish
		SocketState GetState() const;
		// all or nothing, 0 while > 1 MB is unsent
		size_t Send(std::span<const u8> data);
		size_t Receive(std::span<u8> out);
		void Close();

		str GetError() const;
		bool GetPeerCertificateHash(CertificateHash& out) const;
		void SetCallback(SocketCallback callback);
	private:
		std::shared_ptr<TLSCore> _core;
	};

} // namespace Shalltear::Net
