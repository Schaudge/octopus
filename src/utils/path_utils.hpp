// Copyright (c) 2015-2020 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef path_utils_hpp
#define path_utils_hpp

#include <boost/filesystem/path.hpp>
#include <boost/optional.hpp>

namespace octopus {

namespace fs = boost::filesystem;

enum class WorkingDirectoryResolvePolicy { prefer_working_directory, prefer_run_directory };
enum class SymblinkResolvePolicy { resolve, dont_resolve };

boost::optional<fs::path> get_home_directory();

bool is_shorthand_user_path(const fs::path& path) noexcept;

fs::path expand_user_path(const fs::path& path);

fs::path
resolve_path(const fs::path& path, const fs::path& working_directory,
             WorkingDirectoryResolvePolicy wd_policy = WorkingDirectoryResolvePolicy::prefer_working_directory,
             SymblinkResolvePolicy symblink_policy = SymblinkResolvePolicy::dont_resolve);

} // namespace octopus

#endif
