#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Network/TLS.h"

#include <span>
#include <string_view>

namespace Shalltear::Net {

	struct TLSIdentityData {
		CertificateHash hash{};
		void* native = nullptr;
		void (*release)(void* native) = nullptr;

		~TLSIdentityData() {
			if (release) {
				release(native);
			}
		}
	};

} // namespace Shalltear::Net

namespace Shalltear::Net::TLSBackend {

	struct Session;

	enum class Step : u8 {
		Pending,
		Open,
		Closed,
		Failed,
	};

	bool IsAvailable();
	Session* CreateClient(const str& serverName, str& error);
	Session* CreateServer(const TLSIdentityData& identity, str& error);
	void Destroy(Session* session);

	// consumed bytes erased from rawIn, rawOut/plainOut only grow
	Step Advance(Session* session, Vector<u8>& rawIn, Vector<u8>& rawOut, Vector<u8>& plainOut, str& error);
	bool Encrypt(Session* session, std::span<const u8> plain, Vector<u8>& rawOut, str& error);
	void Shutdown(Session* session, Vector<u8>& rawOut);

	bool PeerCertificateHash(Session* session, CertificateHash& out);
	// system trust store/host name, once open
	bool ValidatePeer(Session* session, const str& serverName, str& error);

	bool LoadPKCS12(std::span<const u8> pfx, std::string_view password, TLSIdentityData& out, str& error);

} // namespace Shalltear::Net::TLSBackend
