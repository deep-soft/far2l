#!/bin/bash
# far2l-lang-del-files-3.sh

lng_hlf_files=$(find . -type f \( -name "*.hlf" -o -name "*.lng" \) | grep -E -i -v "(eng|en|ce|re|pe|he).(lng|hlf)");
#size=${#lng_hlf_files}; echo $size;
max_line_len=1; for lng_hlf_file in $lng_hlf_files; do  line_len=${#lng_hlf_file};  if [[ $line_len -gt $max_line_len ]]; then    max_line_len=$line_len;  fi; done; max_line_len=$max_line_len+1;

for lng_hlf_file in $lng_hlf_files; do
  line_len=${#lng_hlf_file};
  pad_to_max="$(($max_line_len-$line_len))";
  eval " printf '[%s]' $lng_hlf_file; printf ' %.s' {1..$pad_to_max}; ";
  if [ -f $lng_hlf_file ]; then
    rm $lng_hlf_file;
    printf " deleted:[%s]\n" $lng_hlf_file;
  fi
done
