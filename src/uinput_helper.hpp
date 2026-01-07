// #include <uinput_helper.cpp>
#pragma once

#include <main.hpp> // Not needed for compile, but vscode wants it for regocnition

bool setup_uinput(stateData*);

void process_remote_message(stateData*, std::vector<std::byte>);