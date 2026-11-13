#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Network/NetworkAPI.h"

#include <functional>
#include <string_view>

namespace Shalltear::Net::HTTP {

	enum class Method : u8 {
		Get,
		Post,
		Put,
		Delete,
		Patch,
		Head,
	};

	enum class Result : u8 {
		Ok,
		Cancelled,
		TimedOut,
		Failed,
	};

	struct Header {
		str name;
		str value;
	};

	struct Request {
		Method method = Method::Get;
		str url;
		Vector<Header> headers{GetMemoryResource(MemTag::Platform)};
		Vector<u8> body{GetMemoryResource(MemTag::Platform)};
		f32 timeoutSeconds = 30.0f;
		u32 maxRedirects = 8;
		// body streamed here, file deleted unless status is 2xx
		str downloadPath;
	};

	struct SHALLTEAR_NETWORK_API Response {
		Result result = Result::Failed;
		u32 status = 0;
		Vector<Header> headers{GetMemoryResource(MemTag::Platform)};
		Vector<u8> body{GetMemoryResource(MemTag::Platform)};
		// after redirects
		str url;
		str error;

		// case-insensitive, 1st match
		std::string_view FindHeader(std::string_view name) const;
	};

	using RequestId = u64;
	using DoneCallback = std::function<void(const Response& response)>;
	using ProgressCallback = std::function<void(u64 received, u64 total)>;

	// callbacks invoked from Net::Dispatch, dropped on module unload
	SHALLTEAR_NETWORK_API RequestId Send(Request request, DoneCallback onDone, ProgressCallback onProgress = {});
	// onDone still invoked (with Result::Cancelled)
	SHALLTEAR_NETWORK_API void Cancel(RequestId id);
	SHALLTEAR_NETWORK_API bool IsAvailable();
	SHALLTEAR_NETWORK_API std::string_view GetMethodName(Method method);

} // namespace Shalltear::Net::HTTP
