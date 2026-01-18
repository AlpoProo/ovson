#pragma once
#include <functional>
#include <string>
#include <unordered_map>

class CommandRegistry {
public:
	using CommandHandler = std::function<void(const std::string& args)>;

	static CommandRegistry& instance();
	void registerCommand(const std::string& name, CommandHandler handler);
	bool tryDispatch(const std::string& message);
    void forEachCommand(const std::function<void(const std::string&)>& visitor);

private:
	std::unordered_map<std::string, CommandHandler> nameToHandler;
};

void RegisterDefaultCommands();


