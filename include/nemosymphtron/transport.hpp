#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace nemosymphtron {

struct ProcessResult {
    int exit_code = -1;
    bool timed_out = false;
    bool output_limited = false;
    std::string out;
    std::string err;
};

// Direct POSIX process execution without invoking a shell, searching PATH, or
// inheriting ambient environment variables.
ProcessResult run_process(const std::string& executable,
                          const std::vector<std::string>& args,
                          const std::string& cwd,
                          const std::map<std::string, std::string>& env,
                          int timeout_ms, std::size_t max_output_bytes);

std::string random_token();

// Same-effective-UID Unix domain socket transport in a mode-0700 non-symlink
// parent directory with kernel peer-UID verification.
int listen_local_socket(const std::string& path);
int connect_local_socket(const std::string& path);
int accept_local_socket(int listening_fd);
void remove_local_socket(const std::string& path);

} // namespace nemosymphtron
