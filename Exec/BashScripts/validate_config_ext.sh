#!/bin/bash

validate_config_ext() {
  local config_file=$1
  local ext="${config_file##*.}"

  case "$ext" in
    ini|json)
      return 0
      ;;
    *)
      echo "Unsupported config extension: .$ext (use .ini or .json)" >&2
      return 1
      ;;
  esac
}
