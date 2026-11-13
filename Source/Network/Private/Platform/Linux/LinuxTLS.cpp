#include "TLSBackend.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <mutex>

namespace Shalltear::Net::TLSBackend {

	namespace {

		// OpenSSL 1.1/3 share this ABI, values from ssl.h/bio.h
		struct SSLMethod;
		struct SSLContext;
		struct SSL;
		struct BIO;
		struct BIOMethod;
		struct X509;
		struct EvpPkey;
		struct EvpMd;
		struct PKCS12;
		struct Stack;

		constexpr int ERROR_WANT_READ = 2;
		constexpr int ERROR_WANT_WRITE = 3;
		constexpr int ERROR_ZERO_RETURN = 6;
		constexpr int CTRL_SET_TLSEXT_HOSTNAME = 55;
		constexpr int CTRL_SET_MIN_PROTO_VERSION = 123;
		constexpr long TLS1_2_VERSION = 0x0303;
		constexpr int BIO_CTRL_PENDING = 10;
		constexpr long X509_V_OK = 0;

		struct OpenSSL {
			const SSLMethod* (*clientMethod)() = nullptr;
			const SSLMethod* (*serverMethod)() = nullptr;
			SSLContext* (*contextNew)(const SSLMethod*) = nullptr;
			void (*contextFree)(SSLContext*) = nullptr;
			int (*contextDefaultVerifyPaths)(SSLContext*) = nullptr;
			int (*contextUseCertificate)(SSLContext*, X509*) = nullptr;
			int (*contextUsePrivateKey)(SSLContext*, EvpPkey*) = nullptr;
			long (*contextCtrl)(SSLContext*, int, long, void*) = nullptr;
			SSL* (*sslNew)(SSLContext*) = nullptr;
			void (*sslFree)(SSL*) = nullptr;
			void (*setBIO)(SSL*, BIO*, BIO*) = nullptr;
			void (*setConnectState)(SSL*) = nullptr;
			void (*setAcceptState)(SSL*) = nullptr;
			int (*doHandshake)(SSL*) = nullptr;
			int (*getError)(const SSL*, int) = nullptr;
			int (*read)(SSL*, void*, int) = nullptr;
			int (*write)(SSL*, const void*, int) = nullptr;
			int (*shutdown)(SSL*) = nullptr;
			long (*ctrl)(SSL*, int, long, void*) = nullptr;
			int (*setHost)(SSL*, const char*) = nullptr;
			long (*verifyResult)(const SSL*) = nullptr;
			X509* (*peerCertificate)(const SSL*) = nullptr;

			BIO* (*bioNew)(const BIOMethod*) = nullptr;
			const BIOMethod* (*bioMemory)() = nullptr;
			int (*bioWrite)(BIO*, const void*, int) = nullptr;
			int (*bioRead)(BIO*, void*, int) = nullptr;
			long (*bioCtrl)(BIO*, int, long, void*) = nullptr;
			void (*x509Free)(X509*) = nullptr;
			int (*x509Digest)(const X509*, const EvpMd*, unsigned char*, unsigned int*) = nullptr;
			const char* (*verifyErrorString)(long) = nullptr;
			const EvpMd* (*sha256)() = nullptr;
			PKCS12* (*readPKCS12)(PKCS12**, const unsigned char**, long) = nullptr;
			int (*parsePKCS12)(PKCS12*, const char*, EvpPkey**, X509**, Stack**) = nullptr;
			void (*pkcs12Free)(PKCS12*) = nullptr;
			void (*pkeyFree)(EvpPkey*) = nullptr;
			void (*stackPopFree)(Stack*, void (*)(void*)) = nullptr;
			unsigned long (*errorGet)() = nullptr;
			void (*errorString)(unsigned long, char*, size_t) = nullptr;

			SSLContext* client = nullptr;
			bool loaded = false;
		};

		void* OpenFirst(std::initializer_list<const char*> names) {
			for (const char* name : names) {
				if (void* image = dlopen(name, RTLD_NOW | RTLD_LOCAL)) {
					return image;
				}
			}
			return nullptr;
		}

		const OpenSSL& Library() {
			static OpenSSL ssl;
			static std::once_flag once;
			std::call_once(once, [] {
				void* crypto = OpenFirst({"libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.so"});
				void* image = crypto ? OpenFirst({"libssl.so.3", "libssl.so.1.1", "libssl.so"}) : nullptr;
				if (!image) {
					return;
				}
				auto bind = [](void* from, auto& target, const char* name) {
					target = reinterpret_cast<std::remove_reference_t<decltype(target)>>(dlsym(from, name));
					return target != nullptr;
				};
				bind(image, ssl.peerCertificate, "SSL_get1_peer_certificate") || bind(image, ssl.peerCertificate, "SSL_get_peer_certificate");
				const bool all =
					ssl.peerCertificate && bind(image, ssl.clientMethod, "TLS_client_method") && bind(image, ssl.serverMethod, "TLS_server_method") &&
					bind(image, ssl.contextNew, "SSL_CTX_new") && bind(image, ssl.contextFree, "SSL_CTX_free") &&
					bind(image, ssl.contextDefaultVerifyPaths, "SSL_CTX_set_default_verify_paths") &&
					bind(image, ssl.contextUseCertificate, "SSL_CTX_use_certificate") &&
					bind(image, ssl.contextUsePrivateKey, "SSL_CTX_use_PrivateKey") && bind(image, ssl.contextCtrl, "SSL_CTX_ctrl") &&
					bind(image, ssl.sslNew, "SSL_new") && bind(image, ssl.sslFree, "SSL_free") && bind(image, ssl.setBIO, "SSL_set_bio") &&
					bind(image, ssl.setConnectState, "SSL_set_connect_state") && bind(image, ssl.setAcceptState, "SSL_set_accept_state") &&
					bind(image, ssl.doHandshake, "SSL_do_handshake") && bind(image, ssl.getError, "SSL_get_error") &&
					bind(image, ssl.read, "SSL_read") && bind(image, ssl.write, "SSL_write") && bind(image, ssl.shutdown, "SSL_shutdown") &&
					bind(image, ssl.ctrl, "SSL_ctrl") && bind(image, ssl.setHost, "SSL_set1_host") &&
					bind(image, ssl.verifyResult, "SSL_get_verify_result") && bind(crypto, ssl.bioNew, "BIO_new") &&
					bind(crypto, ssl.bioMemory, "BIO_s_mem") && bind(crypto, ssl.bioWrite, "BIO_write") && bind(crypto, ssl.bioRead, "BIO_read") &&
					bind(crypto, ssl.bioCtrl, "BIO_ctrl") && bind(crypto, ssl.x509Free, "X509_free") &&
					bind(crypto, ssl.x509Digest, "X509_digest") && bind(crypto, ssl.verifyErrorString, "X509_verify_cert_error_string") &&
					bind(crypto, ssl.sha256, "EVP_sha256") && bind(crypto, ssl.readPKCS12, "d2i_PKCS12") &&
					bind(crypto, ssl.parsePKCS12, "PKCS12_parse") && bind(crypto, ssl.pkcs12Free, "PKCS12_free") &&
					bind(crypto, ssl.pkeyFree, "EVP_PKEY_free") && bind(crypto, ssl.stackPopFree, "OPENSSL_sk_pop_free") &&
					bind(crypto, ssl.errorGet, "ERR_get_error") && bind(crypto, ssl.errorString, "ERR_error_string_n");
				if (!all) {
					return;
				}
				ssl.client = ssl.contextNew(ssl.clientMethod());
				if (!ssl.client) {
					return;
				}
				ssl.contextDefaultVerifyPaths(ssl.client);
				ssl.contextCtrl(ssl.client, CTRL_SET_MIN_PROTO_VERSION, TLS1_2_VERSION, nullptr);
				ssl.loaded = true;
			});
			return ssl;
		}

		str LastError(const OpenSSL& ssl, std::string_view what) {
			const unsigned long code = ssl.errorGet();
			if (code == 0) {
				return str(what);
			}
			char text[256] = {};
			ssl.errorString(code, text, sizeof(text));
			while (ssl.errorGet() != 0) {
			}
			return std::format("{}: {}", what, text);
		}

		struct NativeIdentity {
			X509* certificate = nullptr;
			EvpPkey* key = nullptr;
		};

		void ReleaseIdentity(void* native) {
			auto* identity = static_cast<NativeIdentity*>(native);
			const OpenSSL& ssl = Library();
			if (identity->certificate) {
				ssl.x509Free(identity->certificate);
			}
			if (identity->key) {
				ssl.pkeyFree(identity->key);
			}
			delete identity;
		}

		void FreeX509(void* certificate) {
			Library().x509Free(static_cast<X509*>(certificate));
		}

	} // anonymous namespace

	struct Session {
		SSL* ssl = nullptr;
		SSLContext* owned = nullptr;
		BIO* fromNetwork = nullptr;
		BIO* toNetwork = nullptr;
		bool open = false;
	};

	namespace {

		Session* Create(SSLContext* context, SSLContext* owned, bool server, str& error) {
			const OpenSSL& ssl = Library();
			auto* session = new Session();
			session->owned = owned;
			session->ssl = ssl.sslNew(context);
			session->fromNetwork = ssl.bioNew(ssl.bioMemory());
			session->toNetwork = ssl.bioNew(ssl.bioMemory());
			if (!session->ssl || !session->fromNetwork || !session->toNetwork) {
				error = LastError(ssl, "can't create TLS session");
				Destroy(session);
				return nullptr;
			}
			ssl.setBIO(session->ssl, session->fromNetwork, session->toNetwork);
			if (server) {
				ssl.setAcceptState(session->ssl);
			} else {
				ssl.setConnectState(session->ssl);
			}
			return session;
		}

		void Drain(const OpenSSL& ssl, Session& session, Vector<u8>& rawOut) {
			u8 chunk[16 * 1024];
			while (ssl.bioCtrl(session.toNetwork, BIO_CTRL_PENDING, 0, nullptr) > 0) {
				const int read = ssl.bioRead(session.toNetwork, chunk, sizeof(chunk));
				if (read <= 0) {
					break;
				}
				rawOut.insert(rawOut.end(), chunk, chunk + read);
			}
		}

	} // anonymous namespace

	bool IsAvailable() {
		return Library().loaded;
	}

	Session* CreateClient(const str& serverName, str& error) {
		const OpenSSL& ssl = Library();
		if (!ssl.loaded) {
			error = "no OpenSSL 1.1/3 on this system";
			return nullptr;
		}
		Session* session = Create(ssl.client, nullptr, false, error);
		if (session && !serverName.empty()) {
			ssl.ctrl(session->ssl, CTRL_SET_TLSEXT_HOSTNAME, 0, const_cast<char*>(serverName.c_str()));
			ssl.setHost(session->ssl, serverName.c_str());
		}
		return session;
	}

	Session* CreateServer(const TLSIdentityData& identity, str& error) {
		const OpenSSL& ssl = Library();
		const auto* native = static_cast<const NativeIdentity*>(identity.native);
		if (!ssl.loaded || !native) {
			error = ssl.loaded ? "TLS identity has no certificate" : "no OpenSSL 1.1/3 on this system";
			return nullptr;
		}
		SSLContext* context = ssl.contextNew(ssl.serverMethod());
		if (!context || ssl.contextUseCertificate(context, native->certificate) != 1 || ssl.contextUsePrivateKey(context, native->key) != 1) {
			error = LastError(ssl, "can't use TLS identity");
			if (context) {
				ssl.contextFree(context);
			}
			return nullptr;
		}
		ssl.contextCtrl(context, CTRL_SET_MIN_PROTO_VERSION, TLS1_2_VERSION, nullptr);
		return Create(context, context, true, error);
	}

	void Destroy(Session* session) {
		if (!session) {
			return;
		}
		const OpenSSL& ssl = Library();
		if (session->ssl) {
			ssl.sslFree(session->ssl);
		}
		if (session->owned) {
			ssl.contextFree(session->owned);
		}
		delete session;
	}

	Step Advance(Session* session, Vector<u8>& rawIn, Vector<u8>& rawOut, Vector<u8>& plainOut, str& error) {
		const OpenSSL& ssl = Library();
		if (!rawIn.empty()) {
			ssl.bioWrite(session->fromNetwork, rawIn.data(), static_cast<int>(rawIn.size()));
			rawIn.clear();
		}
		Step step = Step::Open;
		if (!session->open) {
			const int result = ssl.doHandshake(session->ssl);
			if (result == 1) {
				session->open = true;
			} else {
				const int code = ssl.getError(session->ssl, result);
				Drain(ssl, *session, rawOut);
				if (code == ERROR_WANT_READ || code == ERROR_WANT_WRITE) {
					return Step::Pending;
				}
				error = LastError(ssl, "TLS handshake failed");
				return Step::Failed;
			}
		}
		u8 chunk[16 * 1024];
		for (;;) {
			const int read = ssl.read(session->ssl, chunk, sizeof(chunk));
			if (read > 0) {
				plainOut.insert(plainOut.end(), chunk, chunk + read);
				continue;
			}
			const int code = ssl.getError(session->ssl, read);
			if (code == ERROR_ZERO_RETURN) {
				step = Step::Closed;
			} else if (code != ERROR_WANT_READ && code != ERROR_WANT_WRITE) {
				error = LastError(ssl, "TLS read failed");
				step = Step::Failed;
			}
			break;
		}
		Drain(ssl, *session, rawOut);
		return step;
	}

	bool Encrypt(Session* session, std::span<const u8> plain, Vector<u8>& rawOut, str& error) {
		const OpenSSL& ssl = Library();
		while (!plain.empty()) {
			const int chunk = static_cast<int>(std::min<size_t>(plain.size(), 1 << 20));
			const int written = ssl.write(session->ssl, plain.data(), chunk);
			if (written <= 0) {
				error = LastError(ssl, "TLS write failed");
				return false;
			}
			plain = plain.subspan(static_cast<size_t>(written));
		}
		Drain(ssl, *session, rawOut);
		return true;
	}

	void Shutdown(Session* session, Vector<u8>& rawOut) {
		const OpenSSL& ssl = Library();
		if (session->open) {
			ssl.shutdown(session->ssl);
			Drain(ssl, *session, rawOut);
		}
	}

	bool PeerCertificateHash(Session* session, CertificateHash& out) {
		const OpenSSL& ssl = Library();
		X509* certificate = ssl.peerCertificate(session->ssl);
		if (!certificate) {
			return false;
		}
		unsigned int size = 0;
		const bool hashed = ssl.x509Digest(certificate, ssl.sha256(), out.data(), &size) == 1 && size == out.size();
		ssl.x509Free(certificate);
		return hashed;
	}

	bool ValidatePeer(Session* session, const str&, str& error) {
		const OpenSSL& ssl = Library();
		X509* certificate = ssl.peerCertificate(session->ssl);
		if (!certificate) {
			error = "server sent no certificate";
			return false;
		}
		ssl.x509Free(certificate);
		const long result = ssl.verifyResult(session->ssl);
		if (result != X509_V_OK) {
			error = std::format("certificate rejected: {} ({})", ssl.verifyErrorString(result), result);
			return false;
		}
		return true;
	}

	bool LoadPKCS12(std::span<const u8> pfx, std::string_view password, TLSIdentityData& out, str& error) {
		const OpenSSL& ssl = Library();
		if (!ssl.loaded) {
			error = "no OpenSSL 1.1/3 on this system";
			return false;
		}
		const unsigned char* cursor = pfx.data();
		PKCS12* bundle = ssl.readPKCS12(nullptr, &cursor, static_cast<long>(pfx.size()));
		if (!bundle) {
			error = LastError(ssl, "can't read PKCS #12 data");
			return false;
		}
		auto* native = new NativeIdentity();
		Stack* chain = nullptr;
		const str secret(password);
		const bool parsed = ssl.parsePKCS12(bundle, secret.c_str(), &native->key, &native->certificate, &chain) == 1;
		ssl.pkcs12Free(bundle);
		if (chain) {
			ssl.stackPopFree(chain, &FreeX509);
		}
		unsigned int size = 0;
		if (!parsed || !native->certificate || !native->key ||
			ssl.x509Digest(native->certificate, ssl.sha256(), out.hash.data(), &size) != 1) {
			error = parsed ? str("PKCS #12 data has no certificate with private key") : LastError(ssl, "can't open PKCS #12 data");
			ReleaseIdentity(native);
			return false;
		}
		out.native = native;
		out.release = &ReleaseIdentity;
		return true;
	}

} // namespace Shalltear::Net::TLSBackend
