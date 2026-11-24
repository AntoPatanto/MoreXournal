#!/usr/bin/env bash
# shellcheck disable=SC2155

set -o pipefail

# Get the path of the script
SCRIPT_PATH=$(dirname "$(realpath -s "$0")")

# Parse current version string
function current_major_version() {
    sed -n 's/^        VERSION \([0-9]\+\).\([0-9]\+\).\([0-9]\+\)$/\1/p' "${SCRIPT_PATH}/../CMakeLists.txt"
}

function current_minor_version() {
    sed -n 's/^        VERSION \([0-9]\+\).\([0-9]\+\).\([0-9]\+\)$/\2/p' "${SCRIPT_PATH}/../CMakeLists.txt"
}

function current_patch_version() {
    sed -n 's/^        VERSION \([0-9]\+\).\([0-9]\+\).\([0-9]\+\)$/\3/p' "${SCRIPT_PATH}/../CMakeLists.txt"
}

function current_suffix_version() {
    sed -n 's/^set\s*(VERSION_SUFFIX "\([+~][^\"]*\)")/\1/p' "${SCRIPT_PATH}/../CMakeLists.txt"
}

function current_version() {
    echo "$(current_major_version).$(current_minor_version).$(current_patch_version)$(current_suffix_version)"
}

function parse_major_version() {
    echo "$1" | sed -n 's/^\([0-9]\+\)\..*$/\1/p'
}

function parse_minor_version() {
    echo "$1" | sed -n 's/^[0-9]\+\.\([0-9]\+\)\..*$/\1/p'
}

function parse_patch_version() {
    echo "$1" | sed -n 's/^[0-9]\+\.[0-9]\+\.\([0-9]\+\).*$/\1/p'
}

function parse_suffix_version() {
    echo "$1" | sed -n 's/^[0-9]\+\.[0-9]\+\.[0-9]\+\([+~].*\)$/\1/p'
}

# Compares two versions $1 and $2
# If $2 is greater it returns 0
function compare_versions() {
    if [[ $1 == "$2" ]]; then
        return 1
    fi
    if printf "%s\n%s" "$1" "$2" | LC_ALL=C sort -CVu; then
        return 0
    fi
    return 1
}

# Validates the provided version string
# $1 the version as a string
# Returns 1 if the version is valid
function validate_version() {
    if [ -z "$1" ]; then
        return 0
    fi

    if ! [[ $1 =~ ^[0-9]+\.[0-9]+\.[0-9]+([+~][A-Za-z0-9+~\.-]*)*?$ ]]; then
        return 1
    else
        if [[ $1 =~ ~dev$ ]]; then
            return 0
        fi
        return 1
    fi
}

# Print information about the correct format of a version string
function print_version_help() {
    echo "Please supply the version of the release in the format [0-9]+.[0-9]+.[0-9]+([+~][0-9A-Za-z+~.-]*)*"
    echo "The suffix is optional, starts with a '+' or '~' and may only contain alphanumeric characters and '+', '~', '.' and '-'."
    echo "Within the suffix '+' is evaluated as a higher release and '~' as a lower release than the basic version number."
}

# Abort execution
# $1 The exit code
# $2 The message to print before abort
function abort() {
    echo "ABORT: $2"
    exit "$1"
}

# Check if branch already exists
# $1 The name of the branch
# If it exists returns 0
function branch_exists() {
    if git rev-parse --quiet --verify "$1" > /dev/null; then
        return 0
    fi
    return 1
}

# Check if release already exists
# $1 The version string of the release
# If it exists returns 0
function release_exists() {
    if git tag | grep -Fxq "v$1"; then
        return 0
    fi
    return 1
}

# Check if git is in detached head mode
# If yes returns 0
function is_detached() {
    if git status --branch --porcelain | grep -Fxq "## HEAD (no branch)"; then
        return 0
    fi
    return 1
}

# Bumps the version in all relevant places
# Should the prior version be the same version but with an added ~dev this version is replaced
# $1 The version string
# $2 Whether to replace the current version
function bump_version() {
    local prior_version=$(current_version)
    local replace=$2

    if ! [[ "$1" =~ ~dev|\+dev$ ]]; then
        local publish=1
    else
        local publish=0
    fi

    # Update version in CMakeLists.txt
    sed -i "s/^        VERSION $(current_major_version).$(current_minor_version).$(current_patch_version)/        VERSION $(parse_major_version "$1").$(parse_minor_version "$1").$(parse_patch_version "$1")/" "${SCRIPT_PATH}"/../CMakeLists.txt
    sed -i "s/set\s*(VERSION_SUFFIX \"[^\"]*\")/set(VERSION_SUFFIX \"$(parse_suffix_version "$1")\")/g" "${SCRIPT_PATH}/../CMakeLists.txt"

    # From now on current_*_version functions return the new version
    local date=$(date +%Y-%m-%d)

    # Update CHANGELOG.md
    if [ "$replace" -eq 0 ]; then
        sed -i "N;N;s/# Changelog\n\n/# Changelog\n\n## $(current_version) (Unreleased)\n\n/g" "${SCRIPT_PATH}/../CHANGELOG.md"
    else
        if [ $publish -eq 0 ]; then
            sed -i "s/## ${prior_version} (Unreleased)/## $(current_version) (Unreleased)/g" "${SCRIPT_PATH}/../CHANGELOG.md"
        else
            sed -i "s/## ${prior_version} (Unreleased)/## $(current_version) (${date})/g" "${SCRIPT_PATH}/../CHANGELOG.md"
        fi
    fi

    # Update AppStream metadata
    local appdata="${SCRIPT_PATH}/../resources-templates/io.github.antopatanto.morexournal.appdata.xml.in"
    if [ "$replace" -eq 0 ]; then
        sed -i "1,/^    <release .*$/ {/^    <release .*$/i\
        \ \ \ \ <release date=\"$date\" version=\"$(current_version)\" />
        }" "${appdata}"
    else
        sed -i "s/\ \ \ \ <release date=\".*\" version=\"${prior_version}\" \/>/\ \ \ \ <release date=\"$date\" version=\"$(current_version)\" \/>/g" "${appdata}"
    fi

    # Update Fedora RPM spec
    sed -i "s/^Version:[[:space:]]*${prior_version}/Version:        $(current_version)/" "${SCRIPT_PATH}/../rpm/fedora/morexournal.spec"
}

# Prepares a new version
# A new version differs in the major or minor version part from the last.
# Assumes it is on the main development branch
# $1 The version string for the new version
function prepare_new_version() {
    if [[ $(parse_patch_version "$1") -ne 0 ]]; then
        abort 6 "The patch level must be '0'."
    fi

    if ! compare_versions "$(current_version)" "$1"; then
        abort 7 "The provided version is not higher than the current one."
        print_version_help
    fi

    local branch_name="release-$(parse_major_version "$1").$(parse_minor_version "$1")"

    if ! git checkout --quiet -b "$branch_name"; then
        abort 11 "Could not check out new release branch"
    fi

    local new_version="$(echo "$1" | sed 's/+dev$//g')~dev"

    if ! bump_version "$new_version" 1; then
        abort 9 "Could not bump version of release branch"
    fi

    if ! git commit --quiet -a -m "Automated version bump to $new_version"; then
        abort 10 "Could not commit version bump on release branch"
    fi

    echo "SUCCESS: New release $new_version was successfully prepared"
    echo "You are now on the release branch ($branch_name)."
    echo "Please check the last commit on the current branch and master for consistency"
    echo "and push your changes with:"
    echo ""
    echo "    git push origin master $branch_name"
    echo ""
    echo "Should the commit not meet your expectations, you may amend the changes."
    echo "BUT do not modify the version numbers!"
}

# Prepares a hotfix
# A hotfix may only differ in the version suffix from its base release
# Assumes it is on the commit of the release
# $1 The version string for the hotfix
function prepare_hotfix() {
    if [[ $(parse_major_version "$1") -ne $(current_major_version) ]]; then
        abort 6 "The major version may not differ from the base release for a hotfix."
    fi
    if [[ $(parse_minor_version "$1") -ne $(current_minor_version) ]]; then
        abort 6 "The minor version may not differ from the base release for a hotfix."
    fi
    if [[ $(parse_patch_version "$1") -ne $(current_patch_version) ]]; then
        abort 6 "The patch version may not differ from the base release for a hotfix."
    fi

    if ! compare_versions "$(current_version)" "$1"; then
        abort 7 "The provided version is not higher than the release it is based on."
        print_version_help
    fi

    local branch_name="hotfix-$1"

    if branch_exists "$branch_name"; then
        abort 8 "The branch for this release already exists. Use this branch directly instead."
    fi

    if release_exists "$1"; then
        abort 8 "The release already exists."
    fi

    if ! git checkout --quiet -b "$branch_name"; then
        abort 9 "Could not check out new release branch"
    fi

    if ! bump_version "$1~dev" 0; then
        abort 10 "Could not set version of release"
    fi

    if ! git commit --quiet -a -m "Automated version bump to $1"; then
        abort 10 "Could not commit version bump on new release branch"
    fi

    echo "SUCCESS: New release $1 was successfully prepared"
    echo "You are now on the release branch ($branch_name)."
    echo "Please check the last commit on the current branch for consistency and push your changes with:"
    echo ""
    echo "    git push origin"
    echo ""
}

function publish_release() {
    local branch=
    if ! branch=$(git branch --show-current); then
        abort 2 "Could not determine the current branch"
    fi

    if ! [[ $branch =~ ^(release-[0-9]+\.[0-9]+|hotfix-[0-9]+\.[0-9]+\.[0-9]+([+~][A-Za-z0-9+~\.-]*)*)?$ ]]; then
        abort 4 "You are not on a release or hotfix branch. Are you on the right branch?"
    fi

    read -re -p "Are you sure you want to publish release $(current_version|sed 's/~dev$//g')? [y/N] "
    if ! [[ $REPLY =~ ^[Yy]+$ ]]; then
        exit 0
    fi

    # Strip ~dev from the version
    bump_version "$(echo "$(current_version)" | sed 's/~dev$//g')" 1

    if ! git commit -a -m "Release $(current_version)"; then
        abort 5 "Could not commit version bump for release"
    fi

    sha=$(git rev-parse HEAD)
    release_version=$(current_version)

    echo "Tagging the release"
    if ! git tag -a "v$(current_version)" -m "Release $(current_version)"; then
        abort 6 "Could not tag release"
    fi

    # Bump the version to the next patch and add ~dev again if we are on a release branch
    if [[ $branch =~ ^release-[0-9]+\.[0-9]+$ ]]; then
        bump_version "$(current_major_version).$(current_minor_version).$(($(current_patch_version)+1))$(current_suffix_version)~dev" 0

        if ! git commit -a -m "Automated version bump to $(current_version)"; then
            abort 7 "Could not commit version bump after release"
        fi
    fi

    echo "SUCCESS: Release was published locally!"
    echo "To publish the release globally push your changes with:"
    echo ""
    echo "    git push --follow-tags origin ${branch}"
    echo ""

    read -re -p 'Do you want to merge the release back to the main development branch now? [Y/n] '
    if ! [[ $REPLY =~ ^[Nn]+$ ]]; then
        if ! git checkout --quiet master; then
            abort 7 "Could not checkout main development branch"
        fi

        local old_version="$(current_version)"

        if test -x "${SCRIPT_PATH}/../.git/hooks/commit-msg"; then
            abort 8 "Some git hooks already exist. This is not supported by this script."
        fi

        if compare_versions "$old_version" "$release_version"; then
            echo ""
            echo "Be careful while patching! Read these instructions carefully before starting the merge:"
            echo "- Merge your version as if it will be the new version of the main development branch."
            echo "- There should not exist a development version anymore!"
            echo "  A new development version will be created automatically!"

            read -re -p "Start the merging process? [y/N] "
            if ! [[ $REPLY =~ ^[Yy]+$ ]]; then
                if ! git checkout --quiet "$branch"; then
                    abort 7 "Could not checkout release branch. BEWARE you are on 'master' now!"
                fi
                exit 0
            fi

            echo ""

            cat << EOF > "${SCRIPT_PATH}/../.git/hooks/commit-msg"
#!/usr/bin/env bash

SOURCE_ONLY=1
source scripts/release_helper.sh

SCRIPT_PATH="scripts"

if ! [[ "\$(current_version)" == "$release_version" ]]; then
    echo "The version information in CMakeLists.txt is not correct. The current version should be $release_version"
    exit 1
fi

if grep -qE "\+dev|~dev" CHANGELOG.md; then
    echo "CHANGELOG.md should not contain a development version string '[+~]dev'"
    exit 1
fi

if grep -qE "\+dev|~dev" resources-templates/io.github.antopatanto.morexournal.appdata.xml.in; then
    echo "resources-templates/io.github.antopatanto.morexournal.appdata.xml.in should not contain a development version string '[+~]dev'"
    exit 1
fi

bump_version "${release_version}+dev" 0

git add CMakeLists.txt CHANGELOG.md resources-templates/io.github.antopatanto.morexournal.appdata.xml.in rpm/fedora/morexournal.spec

bash -c "git merge HEAD &> /dev/null; while [ \$? -ne 0 ]; do sleep 1; git merge HEAD &> /dev/null; done; git commit --amend -C HEAD --no-verify" &

rm .git/hooks/commit-msg
exit 0
EOF

            chmod +x "${SCRIPT_PATH}/../.git/hooks/commit-msg"
            git merge --no-ff -m "Merge back Release ${release_version}" "$sha"
        else
            echo ""
            echo "Be careful while patching! Read these instructions carefully before starting the merge:"
            echo "- Merge your version as if it is a historic release and do not modify or delete other versions"
            echo "- The versions in the changelog should be ordered in a descending order (highest first)"
            echo "- There should not exist any development version lower than your published release"

            read -re -p "Start the merging process? [y/N] "
            if ! [[ $REPLY =~ ^[Yy]+$ ]]; then
                if ! git checkout --quiet "$branch"; then
                    abort 7 "Could not checkout release branch. BEWARE you are on 'master' now!"
                fi
                exit 0
            fi

            echo ""

            mv "${SCRIPT_PATH}/../.git/hooks/pre-merge-commit.sample" "${SCRIPT_PATH}/../.git/hooks/pre-merge-commit"
            cat << EOF > "${SCRIPT_PATH}/../.git/hooks/pre-commit"
#!/usr/bin/env bash

SOURCE_ONLY=1
source scripts/release_helper.sh

SCRIPT_PATH="scripts"

if ! [[ "\$(current_version)" == "$old_version" ]]; then
    echo "The version information in CMakeLists.txt is not correct. The current version should be $old_version"
    exit 1
fi

if ! [[ "\$(sed -n "N;N;s/# Changelog\n\n## \([^\ ]*\) (Unreleased)/\1/p" CHANGELOG.md)" == "$old_version" ]]; then
    echo "The first version in CHANGELOG.md should be $old_version"
    exit 1
fi

if ! [[ "\$(sed -n "1,/\ \ \ \ <release /{s/\ \ \ \ <release date=\"[^\"]*\" version=\"\([^\"]*\)\" \/>/\1/p}" resources-templates/io.github.antopatanto.morexournal.appdata.xml.in)" == "$old_version" ]]; then
    echo "The first version in resources-templates/io.github.antopatanto.morexournal.appdata.xml.in should be $old_version"
    exit 1
fi

mv .git/hooks/pre-merge-commit .git/hooks/pre-merge-commit.sample
rm .git/hooks/pre-commit
exit 0
EOF

            chmod +x "${SCRIPT_PATH}/../.git/hooks/pre-commit"
            git merge --no-ff -m "Merge back Release ${release_version}" "$sha"
        fi
    fi
}

if [ -z ${SOURCE_ONLY+x} ]; then
    if [ "$#" -lt 1 ]; then
        echo "Missing command"
        command="help"
    else
        command=$1
    fi

    if [[ $command != @(help|prepare|publish) ]]; then
        echo "ABORT: unknown command"
        command="help"
    fi

    if [[ $command == "help" ]]; then
        echo "Usage: $0 <command> <arguments>"
        echo ""
        echo "Commands:"
        echo "    prepare <version>  Prepares a release in the form of a new branch with correct version strings."
        echo "                     This command may only be run on a clean HEAD from:"
        echo "                       - The main development branch (master)"
        echo "                           The supplied version must differ from the current version in the major"
        echo "                           or minor version level. The suffix string may differ in any way."
        echo "                       - A commit of a published release (has a tag starting with 'v')"
        echo "                           The supplied version must differ from the current version in the suffix"
        echo "                           and must be higher than the current version."
        echo "                     The supplied version suffix may not end with '+dev'. This ending is a"
        echo "                     protected suffix and is applied by the script automatically."
        echo ""
        echo "    publish          Publishes a priorly prepared release. You must be on a branch that was"
        echo "                     created during the prepare phase of this script. The version of the release"
        echo "                     is derived from the prepare phase. Make sure to update CHANGELOG.md prior"
        echo "                     to starting this phase."
        echo ""
        echo "    help             Prints this help message"
        echo ""
    fi

    if ! branch=$(git branch --show-current); then
        abort 2 "Could not determine the current branch"
    fi

    if ! git diff --quiet --cached --exit-code > /dev/null; then
        abort 3 "Your working tree is not clean. Please commit or stash all staged changes before running this script."
    fi
    if ! git diff --quiet --exit-code > /dev/null; then
        abort 3 "Your working tree is not clean. Please commit or stash all staged changes before running this script."
    fi

    if [[ $command == "prepare" ]]; then
        version=$2

        if validate_version "$version"; then
            print_version_help
            abort 4 "No valid version string provided"
        fi

        if [[ $version =~ [~+]dev$ ]]; then
            abort 5 "You may not use a suffix ending with '+dev' or '~dev'"
        fi

        if [[ $branch == "master" ]]; then
            if is_detached; then
                abort 6 "You can not prepare a release in detached HEAD mode"
            fi
            prepare_new_version "$version"
        elif [[ $(git tag --contains | grep -Exc '^v[0-9]+.[0-9]+.[0-9]+([+~][0-9A-Za-z+~.-]*)*$') -ne 0 ]]; then
            prepare_hotfix "$version"
        else
            abort 4 "You may only call this script from the main development branch, an existing release branch or a tagged release."
        fi
    elif [[ $command == "publish" ]]; then
        publish_release
    fi
fi
