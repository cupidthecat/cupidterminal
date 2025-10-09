#!/usr/bin/env bash
# Test basic 8 colors, bright colors, and 256-color support

echo -e "\nNormal 8 colors:"
for i in {30..37}; do
  echo -en "\e[${i}mColor ${i} \e[0m"
done
echo -e "\n"

echo -e "Bright colors:"
for i in {90..97}; do
  echo -en "\e[${i}mBright ${i} \e[0m"
done
echo -e "\n"

echo -e "256-color test grid:"
for c in {0..255}; do
  printf "\e[48;5;%sm %3s " "$c" "$c"
  (( (c + 1) % 16 == 0 )) && echo -e "\e[0m"
done
echo -e "\e[0m\nDone."
