#include "Commands.h"
#include "ChatSDK.h"
#include "../Utils/Logger.h"
#include "../Config/Config.h"
#include "ChatInterceptor.h"
#include <sstream>

CommandRegistry &CommandRegistry::instance()
{
	static CommandRegistry inst;
	return inst;
}

void CommandRegistry::registerCommand(const std::string &name, CommandHandler handler)
{
	nameToHandler[name] = std::move(handler);
}

bool CommandRegistry::tryDispatch(const std::string &message)
{
	if (message.empty() || message[0] != '.')
		return false;
	std::string rest = message.substr(1);
	std::istringstream iss(rest);
	std::string cmd;
	iss >> cmd;
	std::string args;
	std::getline(iss, args);
	if (!args.empty() && args[0] == ' ')
		args.erase(0, 1);

	auto it = nameToHandler.find(cmd);
	if (it == nameToHandler.end()){
		ChatSDK::showPrefixed(std::string("§cUnknown command: §f.") + cmd);
		return true;
	}
	try{
		it->second(args);
	}
	catch (...){
		Logger::error("Command threw: %s", cmd.c_str());
	}
	return true;
}

void CommandRegistry::forEachCommand(const std::function<void(const std::string &)> &visitor)
{
	for (std::unordered_map<std::string, CommandHandler>::const_iterator it = nameToHandler.begin(); it != nameToHandler.end(); ++it)
	{
		visitor(it->first);
	}
}

namespace
{
	void cmd_echo(const std::string &args){
		ChatSDK::showPrefixed(args);
	}

	void cmd_help(const std::string &args){
		(void)args;
		std::string list;
		CommandRegistry::instance().forEachCommand([&](const std::string &name)
												   {
			if (!list.empty()) list += ", ";
			list += "." + name; });
		if (list.empty())
			list = "(no commands)";
		ChatSDK::showPrefixed(std::string("§7Commands: §f") + list);
	}

	void cmd_api(const std::string &args){
		std::string trimmed = args;
		while (!trimmed.empty() && trimmed.front() == ' ')
			trimmed.erase(trimmed.begin());
		while (!trimmed.empty() && trimmed.back() == ' ')
			trimmed.pop_back();
		if (trimmed.rfind("new ", 0) == 0)
		{
			std::string key = trimmed.substr(4);
			Config::setApiKey(key);
			ChatSDK::showPrefixed("API key updated.");
			return;
		}
		const std::string &key = Config::getApiKey();
		if (key.empty())
			ChatSDK::showPrefixed("No API key set. Use .api new <key>");
		else
			ChatSDK::showPrefixed(std::string("API KEY: §f") + key);
	}

	void cmd_mode(const std::string &args){
		std::string a = args;
		while (!a.empty() && a.front() == ' ')
			a.erase(a.begin());
		if (a == "bedwars")
		{
			ChatInterceptor::setMode(0);
			ChatSDK::showPrefixed("mode: bedwars");
			return;
		}
		if (a == "skywars")
		{
			ChatInterceptor::setMode(1);
			ChatSDK::showPrefixed("mode: skywars");
			return;
		}
		if (a == "duels")
		{
			ChatInterceptor::setMode(2);
			ChatSDK::showPrefixed("mode: duels");
			return;
		}
		ChatSDK::showPrefixed("usage: .mode bedwars|skywars|duels");
	}
}

void RegisterDefaultCommands(){
	CommandRegistry::instance().registerCommand("echo", cmd_echo);
	CommandRegistry::instance().registerCommand("help", cmd_help);
	CommandRegistry::instance().registerCommand("api", cmd_api);
	CommandRegistry::instance().registerCommand("mode", cmd_mode);
}
