#!/bin/bash
url=$1
curl "${url}?product=gt*" | jq .data | tr -d '"' | base64 -d | tr -s ',' '\n' | cut -d '/' -f 2 | cut -d '-' -f 1 | while read line; do curl "${url}?product=$line&download=on" -o $line.json; done
i=1
echo "IDI_ICON1               ICON        DISCARDABLE            \"icon.ico\"
IDI_ICON2               ICON        DISCARDABLE            \"check.ico\"" > app.rc

echo "<RCC>
    <qresource prefix=\"/icons\">
        <file>icon.ico</file>
        <file>check.ico</file>
    </qresource>
    <qresource prefix=\"/data\">" > resource.qrc
for file in *.json; do echo "<file>"$file"</file>" >> resource.qrc;
echo "IDI_DATA$i               TEXT        DISCARDABLE            \"$file\"" >> app.rc
i=$(($i+1))
done
echo "
</qresource>
</RCC>
" >> resource.qrc
