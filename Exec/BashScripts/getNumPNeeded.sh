#!/bin/bash

# Obtains the number of processes for a given group in a configuration file.
# Parameter 1 - Configuration file name (.ini or .json).
# Parameter 2 - Group index (resizeN / groups[N]).
#====== Do not modify these values =======
config_file=$1
group_index=$2

get_procs_from_ini() {
  local resize_info first_line last_line range_lines

  resize_info=$(grep "\[resize$group_index\]" -n "$config_file" | cut -d ":" -f1)
  first_line=$(echo $resize_info | cut -d " " -f1)
  last_line=$(echo $resize_info | cut -d " " -f2)
  range_lines=$(( last_line - first_line ))
  head -$last_line $config_file | tail -$range_lines | cut -d ';' -f1 | grep Procs | cut -d '=' -f2
}

get_procs_from_json() {
  local procs_line numP

#  procs_line=$(sed -n '/"groups"/,/]/p' "$config_file" \
#    | grep '"Procs"' \
#    | sed -n "$((group_index + 1))p")
  procs_line=$(grep -E '^[[:space:]]*"Procs"[[:space:]]*:' "$config_file" \
    | sed -n "$((group_index + 1))p")

  if [ -z "$procs_line" ]; then
    echo "JSON config: group $group_index not found or missing Procs" >&2
    return 1
  fi

  numP=$(echo "$procs_line" | sed -n 's/.*"Procs"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p')
  if [ -z "$numP" ]; then
    echo "JSON config: invalid Procs in group $group_index" >&2
    return 1
  fi

  echo "$numP"
}

ext="${config_file##*.}"
case "$ext" in
  ini)
    numP=$(get_procs_from_ini)
    ;;
  json)
    numP=$(get_procs_from_json) || exit 1
    ;;
  *)
    echo "Unsupported config extension: .$ext (use .ini or .json)" >&2
    exit 1
    ;;
esac

echo "$numP"
