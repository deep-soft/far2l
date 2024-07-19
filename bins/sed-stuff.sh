#!/bin/bash
#sed-stuff.sh
#2024-07-19 07:20
#macosx latest /opt/homebrew instead of /usr/local
#BOF

export os_name=$("uname")
case $os_name in
  Darwin*)
    echo "Run on OSX"
    echo "Install gnu-sed"
    # brew install gnu-sed
	brew reinstall gnu-sed
    export PATH="/usr/local/opt/gnu-sed/libexec/gnubin:/opt/homebrew/opt/gnu-sed/libexec/gnubin:$PATH"
    # ls -l /usr/local/opt/gnu-sed/libexec/gnubin
	# ls -l /opt/homebrew/opt/gnu-sed/libexec/gnubin
  ;;
  Linux*)
    echo "Run on Linux"
    ;;
  *)
    echo "Run on unknown OS: $os_name"
  ;;
esac

SED_EXE=$(which sed)
echo "SED_EXE=$SED_EXE"
echo "SED_EXE=$SED_EXE" >> $GITHUB_ENV

FILE_TO_CAT="$2";
debug_mode="$DEBUG_MODE_1";
debug_mode_2="$DEBUG_MODE_2";

if [[ "$debug_mode" == "" ]]; then
  debug_mode="N";
fi

if [[ "$debug_mode_2" == "" ]]; then
  debug_mode_2="N";
fi

echo_debug () {
if [[ "$debug_mode" == "Y" ]]; then
  echo "$1";
fi;
}

echo_debug_2 () {
if [[ "$debug_mode_2" == "Y" ]]; then
  echo "$1";
fi;
}

#file_in="sed-stuff.txt"
#file_in="${0%.*}".txt
#echo $file_in

if [[ "$1" != "_" && -f "$1" ]]; then
  file_in="$1";
else
  file_in="${0%.*}".txt;
fi

if [[ -f "$file_in" ]]; then
  _count_=0;
  _countf_=0;
  echo "file_in:[$file_in]";
  cat $file_in | while read -r the_line
  do
    ((count++));
    if [[ ${the_line:0:2} == "S:" ]]; then _str_=$(echo "${the_line:2}" | tr -d "\r"); echo_debug "str:[$_str_]"; fi;
    if [[ ${the_line:0:2} == "R:" ]]; then _rpl_=$(echo "${the_line:2}" | tr -d "\r"); echo_debug "rpl:[$_rpl_]"; fi;
    if [[ ${the_line:0:2} == "F:" ]]; then _fil_=$(echo "${the_line:2}" | tr -d "\r"); echo_debug "fil:[$_fil_]"; fi;
    if [[ "$_fil_" != "" ]]; then
      if [[ -f "$_fil_" ]]; then
        ((countf++));
        echo_debug "$SED_EXE -i \"s|$_str_|$_rpl_|\" $_fil_";
        _str_s_=$(grep "$_str_" "$_fil_");
        echo_debug_2 "CNTF:$countf: $_str_n_";
        $SED_EXE -i "s|$_str_|$_rpl_|" $_fil_;
        _str_r_=$(grep "$_rpl_" "$_fil_");
        if [[ "$_str_s_" != "" ]] && [[ "$_str_r_" != "" ]]; then
          echo "OK_OK: [CNT:$count, CNTF:$countf]";
        else
          echo "NOTOK: [CNT:$count, CNTF:$countf]";
        fi;
        if [[ "$debug_mode" == "Y" ]]; then
          echo "grep 1";
          if [[ "$_str_" != "" ]]; then grep "$_str_" "$_fil_"; else echo ""; fi
          echo "grep 2";
          if [[ "$$_rpl_" != "" ]]; then grep "$$_rpl_" "$_fil_"; else echo ""; fi
        fi;
      else
        echo "not_found: [$_fil_]";
      fi;
      _str_="";
      _rpl_="";
      _fil_="";
    fi;
    # status=$(echo "[CNT:$count, CNTF:$countf]");
  done;
  if [[ "$debug_mode" == "Y" ]]; then
    if [[ "$FILE_TO_CAT" != "" ]]; then
      echo " [[ $FILE_TO_CAT : begin ]]";
      cat $FILE_TO_CAT;
      echo " [[ $FILE_TO_CAT : end ]]";
    fi
  fi
  # echo "$status"
  # not working outside the while loop (runs in subshell process, do not have access to main shell)
fi;

#EOF
