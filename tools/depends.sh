#!/bin/bash
SCRIPT=$(realpath "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

install_deps() {
  echo "** installing build dependencies"
  apt-get update -yqq
  apt-get -y -qq install git cmake gcc g++ zip unzip nasm make python3 curl libfuse3-dev
}

install_cmake() {
  echo "** configuring platform cmake"
  CMAKE_VERSION="$(cmake --version)";
  CMAKE_REGEX="cmake version ([0-9]+\.[0-9]+)\.[0-9]+"
  if [[ $CMAKE_VERSION =~ $CMAKE_REGEX ]]
      then
          regex_match="${BASH_REMATCH[1]}"
          echo "** found cmake version ${regex_match}"
          if [ -d "/usr/share/cmake-${regex_match}" ]; then
              echo "** updating cmake platform in path /usr/share/cmake-${regex_match}"
              cp --verbose "$SCRIPTPATH"/*.cmake /usr/share/cmake-"${regex_match}"/Modules/Platform/
          elif [ -d "/usr/local/share/cmake-${regex_match}" ]; then
              echo "** updating cmake platform in path /usr/local/share/cmake-${regex_match}"
              cp --verbose "$SCRIPTPATH"/*.cmake /usr/local/share/cmake-"${regex_match}"/Modules/Platform/
          fi
  else
      echo "** ERROR: unknown cmake version that regex did not match ${CMAKE_VERSION}"
  fi
}

install_dotnet() {
  echo "** installing dotnet core"
  if ! [ -x "$(command -v dotnet)" ]; then
    "$SCRIPTPATH"/dotnet-install.sh
  fi
}

show_help() {
  echo "usage: depends.sh <command>"
  echo "commands:"
  echo "   install-dotnet        sets up dotnet on the system"
  echo "   install-cmake         installs the vali cmake platform files"
  echo "   install-deps          installs nice to have packages for building"
}

main() {
    if [ $# -eq 0 ]; then
        show_help
        exit 0
    fi

    local subcommand="$1"
    local action=
    while [ $# -gt 0 ]; do
        case "$1" in
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                action=$(echo "$subcommand" | tr '-' '_')
                shift
                break
                ;;
        esac
    done

    if [ -z "$(declare -f "$action")" ]; then
        echo "depends: no such command: $subcommand" >&2
        show_help
        exit 1
    fi

    "$action" "$@"
}

main "$@"
