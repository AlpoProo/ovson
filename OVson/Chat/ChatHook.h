#pragma once
#include <string>

namespace ChatHook {
	bool install();
	void uninstall();
	bool onClientSendMessage(const std::string& message);
}


