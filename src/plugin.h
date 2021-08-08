/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016-2021 katursis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PAWNCMD_PLUGIN_H_
#define PAWNCMD_PLUGIN_H_

class Plugin : public ptl::AbstractPlugin<Plugin, Script, NativeParam> {
 public:
  const char *Name() { return "Pawn.CMD"; }

  int Version() { return PAWNCMD_VERSION; }

  bool OnLoad() {
    ReadConfig();

    InstallHooks();

    RegisterNative<&Script::PC_Init>("PC_Init");

    RegisterNative<&Script::PC_RegAlias, false>("PC_RegAlias");
    RegisterNative<&Script::PC_SetFlags>("PC_SetFlags");
    RegisterNative<&Script::PC_GetFlags>("PC_GetFlags");
    RegisterNative<&Script::PC_RenameCommand>("PC_RenameCommand");
    RegisterNative<&Script::PC_CommandExists>("PC_CommandExists");
    RegisterNative<&Script::PC_DeleteCommand>("PC_DeleteCommand");

    RegisterNative<&Script::PC_GetCommandArray>("PC_GetCommandArray");
    RegisterNative<&Script::PC_GetAliasArray>("PC_GetAliasArray");
    RegisterNative<&Script::PC_GetArraySize>("PC_GetArraySize");
    RegisterNative<&Script::PC_GetCommandName>("PC_GetCommandName");
    RegisterNative<&Script::PC_FreeArray>("PC_FreeArray");

    RegisterNative<&Script::PC_EmulateCommand>("PC_EmulateCommand");

    Log("\n\n"
        "    | %s %s | 2016 - %s"
        "\n"
        "    |--------------------------------"
        "\n"
        "    | Author and maintainer: katursis"
        "\n\n\n"
        "    | Compiled: %s at %s"
        "\n"
        "    |--------------------------------------------------------------"
        "\n"
        "    | Repository: https://github.com/katursis/%s"
        "\n",
        Name(), VersionAsString().c_str(), &__DATE__[7], __DATE__, __TIME__,
        Name());

    return true;
  }

  void OnUnload() {
    SaveConfig();

    Log("plugin unloaded");
  }

  void ReadConfig() {
    std::fstream{config_path_, std::fstream::out | std::fstream::app};

    const auto config = cpptoml::parse_file(config_path_);

    case_insensitivity_ =
        config->get_as<bool>("CaseInsensitivity").value_or(true);
    legacy_opct_support_ =
        config->get_as<bool>("LegacyOpctSupport").value_or(true);
    use_caching_ = config->get_as<bool>("UseCaching").value_or(true);
    locale_ =
        std::locale{config->get_as<std::string>("LocaleName").value_or("C")};
  }

  void SaveConfig() {
    auto config = cpptoml::make_table();

    config->insert("CaseInsensitivity", case_insensitivity_);
    config->insert("LegacyOpctSupport", legacy_opct_support_);
    config->insert("UseCaching", use_caching_);
    config->insert("LocaleName", locale_.name());

    std::fstream{config_path_, std::fstream::out | std::fstream::trunc}
        << (*config);
  }

  bool CaseInsensitivity() const { return case_insensitivity_; }

  bool LegacyOpctSupport() const { return legacy_opct_support_; }

  bool UseCaching() const { return use_caching_; }

  inline std::string ToLower(const std::string &str) {
    try {
      auto result = str;

      std::use_facet<std::ctype<char>>(locale_).tolower(
          &result.front(), &result.front() + result.size());

      return result;
    } catch (const std::exception &e) {
      Log("%s (%s): %s", __func__, str.c_str(), e.what());
    }

    return str;
  }

  void InstallHooks() {
    urmem::address_t addr_opct{};
    urmem::sig_scanner scanner;
    if (!scanner.init(reinterpret_cast<urmem::address_t>(logprintf_)) ||
        !scanner.find(opct_pattern_, opct_mask_, addr_opct)) {
      throw std::runtime_error{"CFilterScripts::OnPlayerCommandText not found"};
    }

    hook_fs__on_player_command_text_ =
        urmem::hook::make(addr_opct, &HOOK_CFilterScripts__OnPlayerCommandText);
  }

  static int THISCALL HOOK_CFilterScripts__OnPlayerCommandText(
      void *, cell playerid, const char *cmdtext) {
    ProcessCommand(playerid, cmdtext);

    return 1;
  }

  static void ProcessCommand(cell playerid, const char *cmdtext) {
    if (!cmdtext || cmdtext[0] != '/') {
      return;
    }

    std::size_t i = 1, cmd_start{};
    while (cmdtext[i] == ' ') {
      i++;
    }  // skip excess spaces before cmd
    cmd_start = i;

    while (cmdtext[i] && cmdtext[i] != ' ') {
      i++;
    }

    std::string cmd{&cmdtext[cmd_start], &cmdtext[i]};

    if (cmd.empty()) {
      return;
    }

    while (cmdtext[i] == ' ') {
      i++;
    }  // skip excess spaces after cmd

    const char *params = &cmdtext[i];

    cmd = Script::PrepareCommandName(cmd);

    Plugin::EveryScript([=](auto &script) {
      return script->HandleCommand(playerid, cmdtext, cmd, params);
    });
  }

 private:
#ifdef _WIN32
  const char *opct_pattern_ =
      "\x83\xEC\x08\x53\x8B\x5C\x24\x14\x55\x8B\x6C\x24\x14\x56\x33\xF6\x57"
      "\x8B\xF9\x89\x74\x24\x10\x8B\x04\xB7\x85\xC0";
  const char *opct_mask_ = "xxxxxxxxxxxxxxxxxxxxxxxxxxxx";
#else
  const char *opct_pattern_ =
      "\x55\x89\xE5\x57\x56\x53\x83\xEC\x2C\x8B\x75\x08\xC7\x45\xE4\x00\x00"
      "\x00\x00\x8B\x7D\x10\x89\xF3\xEB\x14";
  const char *opct_mask_ = "xxxxxxxxxxxxxxxxxxxxxxxxxx";
#endif

  const std::string config_path_ = "plugins/pawncmd.cfg";

  bool case_insensitivity_{};
  bool legacy_opct_support_{};
  bool use_caching_{};
  std::locale locale_;

  std::shared_ptr<urmem::hook> hook_fs__on_player_command_text_;
};

#endif  // PAWNCMD_PLUGIN_H_
