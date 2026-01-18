#pragma once
#include <string>

namespace Http {
	bool get(const std::string& url, std::string& responseBody, const std::string& headerName = std::string(), const std::string& headerValue = std::string());
    bool postJson(const std::string& url, const std::string& jsonBody, std::string& responseBody);
}


