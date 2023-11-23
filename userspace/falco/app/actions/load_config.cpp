// SPDX-License-Identifier: Apache-2.0
/*
Copyright (C) 2023 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "actions.h"
#include "falco_utils.h"

/* DEPRECATED: we will remove it in Falco 0.38. */
#define FALCO_BPF_ENV_VARIABLE "FALCO_BPF_PROBE"

using namespace falco::app;
using namespace falco::app::actions;

// applies legacy/in-deprecation options to the current state
static falco::app::run_result apply_deprecated_options(falco::app::state& s)
{
	// Please note: is not possible to mix command line options and configs to obtain a configuration
	// we need to use only one method. For example, is not possible to set the gvisor-config through
	// the command line and the gvisor-root through the config file. For this reason, if we detect
	// at least one change in the default config we don't allow to use the command line options.
	if(s.config->m_changes_in_engine_config)
	{
		falco_logger::log(falco_logger::level::WARNING,
				  "Since the new 'engine' config key is being used, deprecated CLI options "
				  "[-e,-g,--gvisor-config,--nodriver,--modern-bpf] and FALCO_BPF_PROBE environment variable will be ignored.\n");
		return run_result::ok(); 
	}

	// Replace the kmod default values in case the engine was open with the kmod.
	// We don't have a command line option to open the kmod so we have to always enforce the
	// default values.
	s.config->m_kmod.m_drop_failed_exit = s.config->m_syscall_drop_failed_exit;
	s.config->m_kmod.m_buf_size_preset = s.config->m_syscall_buf_size_preset;

	// If overridden from CLI options (soon to be removed),
	// use the requested driver.
	if (getenv(FALCO_BPF_ENV_VARIABLE))
	{
		falco_logger::log(falco_logger::level::WARNING, "DEPRECATION NOTICE: the FALCO_BPF_PROBE environment variable is deprecated and will be removed in Falco 0.38!\n");
		s.config->m_engine_mode = engine_kind_t::EBPF;
		s.config->m_ebpf.m_probe_path = getenv(FALCO_BPF_ENV_VARIABLE);
		s.config->m_ebpf.m_drop_failed_exit = s.config->m_syscall_drop_failed_exit;
		s.config->m_ebpf.m_buf_size_preset = s.config->m_syscall_buf_size_preset;
	}
	else if (s.options.modern_bpf)
	{
		falco_logger::log(falco_logger::level::WARNING, "DEPRECATION NOTICE: the '--modern-bpf' command line option is deprecated and will be removed in Falco 0.38!\n");
		s.config->m_engine_mode = engine_kind_t::MODERN_EBPF;
		s.config->m_modern_ebpf.m_drop_failed_exit = s.config->m_syscall_drop_failed_exit;
		s.config->m_modern_ebpf.m_buf_size_preset = s.config->m_syscall_buf_size_preset;
		s.config->m_modern_ebpf.m_cpus_for_each_buffer = s.config->m_cpus_for_each_syscall_buffer;
	}
	if (!s.options.gvisor_config.empty())
	{
		falco_logger::log(falco_logger::level::WARNING, "DEPRECATION NOTICE: the '-g,--gvisor-config' command line option is deprecated and will be removed in Falco 0.38!\n");
		s.config->m_engine_mode =  engine_kind_t::GVISOR;
		s.config->m_gvisor.m_config = s.options.gvisor_config;
		s.config->m_gvisor.m_root = s.options.gvisor_root;
	}
	if (s.options.nodriver)
	{
		falco_logger::log(falco_logger::level::WARNING, "DEPRECATION NOTICE: the '--nodriver' command line option is deprecated and will be removed in Falco 0.38!\n");
		s.config->m_engine_mode =  engine_kind_t::NONE;
	}
	if (!s.options.capture_file.empty())
	{
		falco_logger::log(falco_logger::level::WARNING, "DEPRECATION NOTICE: the '-e' command line option is deprecated and will be removed in Falco 0.38!\n");
		s.config->m_engine_mode = engine_kind_t::REPLAY;
		s.config->m_replay.m_capture_file = s.options.capture_file;
	}
	return run_result::ok();
}

falco::app::run_result falco::app::actions::load_config(falco::app::state& s)
{
	try
	{
		if (!s.options.conf_filename.empty())
		{
			s.config->init(s.options.conf_filename, s.options.cmdline_config_options);
		}
		else
		{
			// Is possible to have an empty config file when we want to use some command line
			// options like `--help`, `--version`, ...
			// The configs used in `load_yaml` will be initialized to the default values.
			s.config->init(s.options.cmdline_config_options);
		}
	}
	catch (std::exception& e)
	{
		return run_result::fatal(e.what());
	}

	// log after config init because config determines where logs go
	falco_logger::set_time_format_iso_8601(s.config->m_time_format_iso_8601);
	falco_logger::log(falco_logger::level::INFO, "Falco version: " + std::string(FALCO_VERSION) + " (" + std::string(FALCO_TARGET_ARCH) + ")\n");
	if (!s.cmdline.empty())
	{
		falco_logger::log(falco_logger::level::DEBUG, "CLI args: " + s.cmdline);
	}
	if (!s.options.conf_filename.empty())
	{
		falco_logger::log(falco_logger::level::INFO, "Falco initialized with configuration file: " + s.options.conf_filename + "\n");
	}

	s.config->m_buffered_outputs = !s.options.unbuffered_outputs;

	return apply_deprecated_options(s);
}

falco::app::run_result falco::app::actions::require_config_file(falco::app::state& s)
{
#ifndef __EMSCRIPTEN__
	if (s.options.conf_filename.empty())
	{
#ifndef BUILD_TYPE_RELEASE
		return run_result::fatal(std::string("You must create a config file at ")  + FALCO_SOURCE_CONF_FILE + ", " + FALCO_INSTALL_CONF_FILE + " or by passing -c");
#else // BUILD_TYPE_RELEASE
		return run_result::fatal(std::string("You must create a config file at ")  + FALCO_INSTALL_CONF_FILE + " or by passing -c");
#endif // BUILD_TYPE_RELEASE
	}
#endif // __EMSCRIPTEN__
	return run_result::ok();
}
