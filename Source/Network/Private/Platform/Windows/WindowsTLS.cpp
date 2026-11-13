#include "TLSBackend.h"

#include "Shalltear/Core/StringUtil.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <schannel.h>
#include <security.h>

#include <algorithm>
#include <cstring>
#include <format>

namespace Shalltear::Net::TLSBackend {

	struct Session {
		CredHandle credentials{};
		bool hasCredentials = false;
		CtxtHandle context{};
		bool hasContext = false;
		bool server = false;
		bool open = false;
		std::wstring serverName;
		SecPkgContext_StreamSizes sizes{};
	};

	namespace {

		// SECURITY_FLAG_IGNORE_REVOCATION, from wininet.h
		constexpr DWORD IGNORE_REVOCATION = 0x00000080;
		constexpr DWORD CLIENT_FLAGS = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
									   ISC_REQ_STREAM | ISC_REQ_EXTENDED_ERROR | ISC_REQ_MANUAL_CRED_VALIDATION;
		constexpr DWORD SERVER_FLAGS = ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY | ASC_REQ_ALLOCATE_MEMORY |
									   ASC_REQ_STREAM | ASC_REQ_EXTENDED_ERROR;

		struct NativeIdentity {
			HCERTSTORE store = nullptr;
			PCCERT_CONTEXT certificate = nullptr;
		};

		str ErrorText(DWORD code) {
			wchar_t* buffer = nullptr;
			const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code,
												0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
			str text = length > 0 ? StringUtil::WideToUTF8(std::wstring_view(buffer, length)) : str{};
			if (buffer) {
				LocalFree(buffer);
			}
			while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == '.' || text.back() == ' ')) {
				text.pop_back();
			}
			return std::format("{} (0x{:08X})", text.empty() ? "TLS error" : text, static_cast<u32>(code));
		}

		void TakeToken(SecBuffer& token, Vector<u8>& rawOut) {
			if (token.pvBuffer && token.cbBuffer > 0) {
				const u8* bytes = static_cast<const u8*>(token.pvBuffer);
				rawOut.insert(rawOut.end(), bytes, bytes + token.cbBuffer);
			}
			if (token.pvBuffer) {
				FreeContextBuffer(token.pvBuffer);
				token.pvBuffer = nullptr;
			}
		}

		void KeepExtra(const SecBuffer& extra, Vector<u8>& rawIn) {
			if (extra.BufferType == SECBUFFER_EXTRA && extra.cbBuffer > 0 && extra.cbBuffer <= rawIn.size()) {
				rawIn.erase(rawIn.begin(), rawIn.end() - static_cast<ptrdiff_t>(extra.cbBuffer));
			} else {
				rawIn.clear();
			}
		}

		bool Acquire(Session& session, SCHANNEL_CRED& credential, DWORD use, str& error) {
			TimeStamp expiry{};
			const SECURITY_STATUS status = AcquireCredentialsHandleW(nullptr, const_cast<LPWSTR>(UNISP_NAME_W), use, nullptr, &credential, nullptr,
																	 nullptr, &session.credentials, &expiry);
			if (status != SEC_E_OK) {
				error = "can't acquire TLS credentials: " + ErrorText(static_cast<DWORD>(status));
				return false;
			}
			session.hasCredentials = true;
			return true;
		}

		Step HandshakeOnce(Session& session, Vector<u8>& rawIn, Vector<u8>& rawOut, str& error) {
			const bool first = !session.hasContext;
			const bool gaveInput = session.server || !first;
			if (gaveInput && rawIn.empty()) {
				return Step::Pending;
			}
			SecBuffer input[2] = {};
			input[0].BufferType = SECBUFFER_TOKEN;
			input[0].pvBuffer = rawIn.data();
			input[0].cbBuffer = static_cast<unsigned long>(rawIn.size());
			input[1].BufferType = SECBUFFER_EMPTY;
			SecBufferDesc inputDesc{SECBUFFER_VERSION, 2, input};
			SecBuffer output[1] = {};
			output[0].BufferType = SECBUFFER_TOKEN;
			SecBufferDesc outputDesc{SECBUFFER_VERSION, 1, output};
			unsigned long attributes = 0;
			TimeStamp expiry{};
			SECURITY_STATUS status = 0;
			if (session.server) {
				status = AcceptSecurityContext(&session.credentials, first ? nullptr : &session.context, &inputDesc, SERVER_FLAGS, 0,
											   &session.context, &outputDesc, &attributes, &expiry);
			} else {
				status = InitializeSecurityContextW(&session.credentials, first ? nullptr : &session.context,
													session.serverName.empty() ? nullptr : session.serverName.data(), CLIENT_FLAGS, 0, 0,
													first ? nullptr : &inputDesc, 0, &session.context, &outputDesc, &attributes, &expiry);
			}
			const bool progressed = status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED || status == SEC_I_INCOMPLETE_CREDENTIALS;
			session.hasContext = session.hasContext || progressed;
			TakeToken(output[0], rawOut);
			if (status == SEC_E_INCOMPLETE_MESSAGE) {
				return Step::Pending;
			}
			if (!progressed) {
				error = "TLS handshake failed: " + ErrorText(static_cast<DWORD>(status));
				return Step::Failed;
			}
			if (gaveInput) {
				KeepExtra(input[1], rawIn);
			}
			if (status != SEC_E_OK) {
				return Step::Pending;
			}
			const SECURITY_STATUS sizes = QueryContextAttributesW(&session.context, SECPKG_ATTR_STREAM_SIZES, &session.sizes);
			if (sizes != SEC_E_OK) {
				error = "TLS stream sizes unavailable: " + ErrorText(static_cast<DWORD>(sizes));
				return Step::Failed;
			}
			session.open = true;
			return Step::Open;
		}

		Step Handshake(Session& session, Vector<u8>& rawIn, Vector<u8>& rawOut, str& error) {
			for (;;) {
				const size_t before = rawIn.size();
				const Step step = HandshakeOnce(session, rawIn, rawOut, error);
				if (step != Step::Pending || rawIn.empty() || rawIn.size() == before) {
					return step;
				}
			}
		}

		Step Decrypt(Session& session, Vector<u8>& rawIn, Vector<u8>& rawOut, Vector<u8>& plainOut, str& error) {
			while (!rawIn.empty()) {
				SecBuffer buffers[4] = {};
				buffers[0].BufferType = SECBUFFER_DATA;
				buffers[0].pvBuffer = rawIn.data();
				buffers[0].cbBuffer = static_cast<unsigned long>(rawIn.size());
				buffers[1].BufferType = SECBUFFER_EMPTY;
				buffers[2].BufferType = SECBUFFER_EMPTY;
				buffers[3].BufferType = SECBUFFER_EMPTY;
				SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};
				const SECURITY_STATUS status = DecryptMessage(&session.context, &desc, 0, nullptr);
				if (status == SEC_E_INCOMPLETE_MESSAGE) {
					return Step::Open;
				}
				if (status == SEC_I_CONTEXT_EXPIRED) {
					rawIn.clear();
					return Step::Closed;
				}
				if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE) {
					error = "TLS decrypt failed: " + ErrorText(static_cast<DWORD>(status));
					return Step::Failed;
				}
				Vector<u8> extra{GetMemoryResource(MemTag::Platform)};
				for (const SecBuffer& buffer : buffers) {
					const u8* bytes = static_cast<const u8*>(buffer.pvBuffer);
					if (buffer.BufferType == SECBUFFER_DATA && bytes) {
						plainOut.insert(plainOut.end(), bytes, bytes + buffer.cbBuffer);
					} else if (buffer.BufferType == SECBUFFER_EXTRA && bytes) {
						extra.assign(bytes, bytes + buffer.cbBuffer);
					}
				}
				rawIn.swap(extra);
				if (status == SEC_I_RENEGOTIATE) {
					if (session.server) {
						error = "TLS renegotiation isn't supported";
						return Step::Failed;
					}
					SecBuffer input[2] = {};
					input[0].BufferType = SECBUFFER_TOKEN;
					input[0].pvBuffer = rawIn.data();
					input[0].cbBuffer = static_cast<unsigned long>(rawIn.size());
					input[1].BufferType = SECBUFFER_EMPTY;
					SecBufferDesc inputDesc{SECBUFFER_VERSION, 2, input};
					SecBuffer output[1] = {};
					output[0].BufferType = SECBUFFER_TOKEN;
					SecBufferDesc outputDesc{SECBUFFER_VERSION, 1, output};
					unsigned long attributes = 0;
					TimeStamp expiry{};
					const SECURITY_STATUS again = InitializeSecurityContextW(
						&session.credentials, &session.context, session.serverName.empty() ? nullptr : session.serverName.data(), CLIENT_FLAGS, 0,
						0, &inputDesc, 0, nullptr, &outputDesc, &attributes, &expiry);
					TakeToken(output[0], rawOut);
					if (FAILED(again)) {
						error = "TLS post-handshake message failed: " + ErrorText(static_cast<DWORD>(again));
						return Step::Failed;
					}
					KeepExtra(input[1], rawIn);
				}
			}
			return Step::Open;
		}

		void DeleteKey(PCCERT_CONTEXT certificate) {
			DWORD size = 0;
			if (!CertGetCertificateContextProperty(certificate, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &size) || size == 0) {
				return;
			}
			Vector<u8> buffer{GetMemoryResource(MemTag::Platform)};
			buffer.resize(size);
			auto* info = reinterpret_cast<CRYPT_KEY_PROV_INFO*>(buffer.data());
			if (!CertGetCertificateContextProperty(certificate, CERT_KEY_PROV_INFO_PROP_ID, info, &size)) {
				return;
			}
			if (info->dwProvType == 0) {
				NCRYPT_PROV_HANDLE provider = 0;
				if (NCryptOpenStorageProvider(&provider, info->pwszProvName, 0) == ERROR_SUCCESS) {
					NCRYPT_KEY_HANDLE key = 0;
					if (NCryptOpenKey(provider, &key, info->pwszContainerName, 0, info->dwFlags & NCRYPT_MACHINE_KEY_FLAG) == ERROR_SUCCESS) {
						NCryptDeleteKey(key, 0);
					}
					NCryptFreeObject(provider);
				}
				return;
			}
			HCRYPTPROV provider = 0;
			CryptAcquireContextW(&provider, info->pwszContainerName, info->pwszProvName, info->dwProvType,
								 CRYPT_DELETEKEYSET | (info->dwFlags & CRYPT_MACHINE_KEYSET));
		}

		void ReleaseIdentity(void* native) {
			auto* identity = static_cast<NativeIdentity*>(native);
			if (identity->store) {
				for (PCCERT_CONTEXT held = CertEnumCertificatesInStore(identity->store, nullptr); held;
					 held = CertEnumCertificatesInStore(identity->store, held)) {
					DeleteKey(held);
				}
			}
			if (identity->certificate) {
				CertFreeCertificateContext(identity->certificate);
			}
			if (identity->store) {
				CertCloseStore(identity->store, 0);
			}
			delete identity;
		}

		bool HashOf(PCCERT_CONTEXT certificate, CertificateHash& out) {
			DWORD size = static_cast<DWORD>(out.size());
			return CertGetCertificateContextProperty(certificate, CERT_SHA256_HASH_PROP_ID, out.data(), &size) && size == out.size();
		}

	} // anonymous namespace

	bool IsAvailable() {
		return true;
	}

	Session* CreateClient(const str& serverName, str& error) {
		auto* session = new Session();
		session->serverName = StringUtil::UTF8ToWide(serverName);
		SCHANNEL_CRED credential{};
		credential.dwVersion = SCHANNEL_CRED_VERSION;
		credential.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
		if (!Acquire(*session, credential, SECPKG_CRED_OUTBOUND, error)) {
			Destroy(session);
			return nullptr;
		}
		return session;
	}

	Session* CreateServer(const TLSIdentityData& identity, str& error) {
		const auto* native = static_cast<const NativeIdentity*>(identity.native);
		if (!native || !native->certificate) {
			error = "TLS identity has no certificate";
			return nullptr;
		}
		auto* session = new Session();
		session->server = true;
		PCCERT_CONTEXT certificates[] = {native->certificate};
		SCHANNEL_CRED credential{};
		credential.dwVersion = SCHANNEL_CRED_VERSION;
		credential.cCreds = 1;
		credential.paCred = certificates;
		credential.dwFlags = SCH_USE_STRONG_CRYPTO;
		if (!Acquire(*session, credential, SECPKG_CRED_INBOUND, error)) {
			Destroy(session);
			return nullptr;
		}
		return session;
	}

	void Destroy(Session* session) {
		if (!session) {
			return;
		}
		if (session->hasContext) {
			DeleteSecurityContext(&session->context);
		}
		if (session->hasCredentials) {
			FreeCredentialsHandle(&session->credentials);
		}
		delete session;
	}

	Step Advance(Session* session, Vector<u8>& rawIn, Vector<u8>& rawOut, Vector<u8>& plainOut, str& error) {
		if (!session->open) {
			const Step step = Handshake(*session, rawIn, rawOut, error);
			if (step != Step::Open) {
				return step;
			}
		}
		return Decrypt(*session, rawIn, rawOut, plainOut, error);
	}

	bool Encrypt(Session* session, std::span<const u8> plain, Vector<u8>& rawOut, str& error) {
		const SecPkgContext_StreamSizes& sizes = session->sizes;
		Vector<u8> record{GetMemoryResource(MemTag::Platform)};
		while (!plain.empty()) {
			const size_t chunk = std::min<size_t>(plain.size(), sizes.cbMaximumMessage);
			record.assign(sizes.cbHeader + chunk + sizes.cbTrailer, 0);
			std::memcpy(record.data() + sizes.cbHeader, plain.data(), chunk);
			SecBuffer buffers[4] = {};
			buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
			buffers[0].pvBuffer = record.data();
			buffers[0].cbBuffer = sizes.cbHeader;
			buffers[1].BufferType = SECBUFFER_DATA;
			buffers[1].pvBuffer = record.data() + sizes.cbHeader;
			buffers[1].cbBuffer = static_cast<unsigned long>(chunk);
			buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
			buffers[2].pvBuffer = record.data() + sizes.cbHeader + chunk;
			buffers[2].cbBuffer = sizes.cbTrailer;
			buffers[3].BufferType = SECBUFFER_EMPTY;
			SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};
			const SECURITY_STATUS status = EncryptMessage(&session->context, 0, &desc, 0);
			if (status != SEC_E_OK) {
				error = "TLS encrypt failed: " + ErrorText(static_cast<DWORD>(status));
				return false;
			}
			const size_t written = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
			rawOut.insert(rawOut.end(), record.begin(), record.begin() + static_cast<ptrdiff_t>(written));
			plain = plain.subspan(chunk);
		}
		return true;
	}

	void Shutdown(Session* session, Vector<u8>& rawOut) {
		if (!session->hasContext) {
			return;
		}
		DWORD token = SCHANNEL_SHUTDOWN;
		SecBuffer control[1] = {};
		control[0].BufferType = SECBUFFER_TOKEN;
		control[0].pvBuffer = &token;
		control[0].cbBuffer = sizeof(token);
		SecBufferDesc controlDesc{SECBUFFER_VERSION, 1, control};
		if (ApplyControlToken(&session->context, &controlDesc) != SEC_E_OK) {
			return;
		}
		SecBuffer output[1] = {};
		output[0].BufferType = SECBUFFER_TOKEN;
		SecBufferDesc outputDesc{SECBUFFER_VERSION, 1, output};
		unsigned long attributes = 0;
		TimeStamp expiry{};
		if (session->server) {
			AcceptSecurityContext(&session->credentials, &session->context, nullptr, SERVER_FLAGS, 0, nullptr, &outputDesc, &attributes, &expiry);
		} else {
			InitializeSecurityContextW(&session->credentials, &session->context, nullptr, CLIENT_FLAGS, 0, 0, nullptr, 0, nullptr, &outputDesc,
									   &attributes, &expiry);
		}
		TakeToken(output[0], rawOut);
	}

	bool PeerCertificateHash(Session* session, CertificateHash& out) {
		PCCERT_CONTEXT certificate = nullptr;
		if (!session->hasContext ||
			QueryContextAttributesW(&session->context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &certificate) != SEC_E_OK || !certificate) {
			return false;
		}
		const bool hashed = HashOf(certificate, out);
		CertFreeCertificateContext(certificate);
		return hashed;
	}

	bool ValidatePeer(Session* session, const str& serverName, str& error) {
		PCCERT_CONTEXT certificate = nullptr;
		if (QueryContextAttributesW(&session->context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &certificate) != SEC_E_OK || !certificate) {
			error = "server sent no certificate";
			return false;
		}
		LPSTR usages[] = {const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH)};
		CERT_CHAIN_PARA chainPara{};
		chainPara.cbSize = sizeof(chainPara);
		chainPara.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
		chainPara.RequestedUsage.Usage.cUsageIdentifier = 1;
		chainPara.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
		PCCERT_CHAIN_CONTEXT chain = nullptr;
		// frame should not stall on validation
		if (!CertGetCertificateChain(nullptr, certificate, nullptr, certificate->hCertStore, &chainPara, CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL, nullptr,
									 &chain)) {
			error = "can't build certificate chain: " + ErrorText(GetLastError());
			CertFreeCertificateContext(certificate);
			return false;
		}
		std::wstring name = StringUtil::UTF8ToWide(serverName);
		SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
		ssl.cbSize = sizeof(ssl);
		ssl.dwAuthType = AUTHTYPE_SERVER;
		ssl.fdwChecks = IGNORE_REVOCATION;
		ssl.pwszServerName = name.data();
		CERT_CHAIN_POLICY_PARA policyPara{};
		policyPara.cbSize = sizeof(policyPara);
		policyPara.pvExtraPolicyPara = &ssl;
		CERT_CHAIN_POLICY_STATUS policyStatus{};
		policyStatus.cbSize = sizeof(policyStatus);
		const bool checked = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policyPara, &policyStatus);
		CertFreeCertificateChain(chain);
		CertFreeCertificateContext(certificate);
		if (!checked) {
			error = "can't check certificate chain: " + ErrorText(GetLastError());
			return false;
		}
		if (policyStatus.dwError != 0) {
			error = "certificate rejected: " + ErrorText(policyStatus.dwError);
			return false;
		}
		return true;
	}

	bool LoadPKCS12(std::span<const u8> pfx, std::string_view password, TLSIdentityData& out, str& error) {
		CRYPT_DATA_BLOB blob{};
		blob.cbData = static_cast<DWORD>(pfx.size());
		blob.pbData = const_cast<BYTE*>(pfx.data());
		const std::wstring widePassword = StringUtil::UTF8ToWide(password);
		// key persisted until release, in-memory keys unusable by SChannel in LSA
		HCERTSTORE store = PFXImportCertStore(&blob, widePassword.c_str(), CRYPT_USER_KEYSET);
		if (!store) {
			error = "can't read PKCS #12 data: " + ErrorText(GetLastError());
			return false;
		}
		PCCERT_CONTEXT found = nullptr;
		for (PCCERT_CONTEXT certificate = CertEnumCertificatesInStore(store, nullptr); certificate;
			 certificate = CertEnumCertificatesInStore(store, certificate)) {
			DWORD keySpec = 0;
			BOOL release = FALSE;
			HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
			if (CryptAcquireCertificatePrivateKey(certificate, CRYPT_ACQUIRE_CACHE_FLAG | CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG,
												  nullptr, &key, &keySpec, &release)) {
				found = CertDuplicateCertificateContext(certificate);
				CertFreeCertificateContext(certificate);
				break;
			}
		}
		if (!found) {
			CertCloseStore(store, 0);
			error = "PKCS #12 data has no certificate with private key";
			return false;
		}
		auto* native = new NativeIdentity{store, found};
		if (!HashOf(found, out.hash)) {
			ReleaseIdentity(native);
			error = "can't hash certificate";
			return false;
		}
		out.native = native;
		out.release = &ReleaseIdentity;
		return true;
	}

} // namespace Shalltear::Net::TLSBackend
