#!/bin/sh
#
# json dbus bridge
#
# Copyright (c) 2010 by Michael Olbrich <m.olbrich@pengutronix.de>
#
# To the extent possible under law, Pengutronix e.K. has waived all copyright
# and related or neighboring rights to JSON-DBus-Bridge Demo Code.
#

url="http://localhost:8080/rpc"
data='{"service":"com.pengutronix.jdb.simpleinfo|/","method":"com.pengutronix.jdb.SystemInfo.Ping","id":0,"params":["s", "foo"]}'

dd=`wget -O - -q --header="Referer: $url" --post-data="$data" "$url"`

if [ $? -ne 0 ]; then
	echo "wget failed. retrying:"
	wget -O - --header="Referer: $url" --post-data="$data" "$url"
	exit 1
fi

if [ "x$dd" != 'x{ "id": 0, "error": null, "result": "foo" }' ]; then
	echo got wrong data from com.pengutronix.jdb.simpleinfo
	echo expected:
	echo '{ "id": 0, "error": null, "result": "foo" }'
	echo got:
	echo "$dd"
	exit 1
fi

echo Fetching data from com.pengutronix.jdb.simpleinfo sucessful.

