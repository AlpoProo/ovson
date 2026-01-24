#pragma once
#include <functional>

namespace RenderHook {
	bool install();
	void uninstall();
	void poll();

    void enqueueTask(std::function<void()> task);
    float getDelta();
}
