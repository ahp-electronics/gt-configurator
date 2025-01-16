#!/bin/bash
url=$1
curl "${url}?product=gt*" | jq .data | tr -d '"' | base64 -d | tr -s ',' '\n' | cut -d '/' -f 2 | cut -d '-' -f 1 | while read line; do curl "${url}?product=$line&download=on" -o $line.json; done
