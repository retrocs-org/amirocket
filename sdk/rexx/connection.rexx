/* connection.rexx CONNECT|DISCONNECT server-number|ALL [AMIROCKET.2] */
parse upper arg action server client
if action <> 'CONNECT' & action <> 'DISCONNECT' then exit 10
if server = '' then exit 10
if client = '' then client = 'AMIROCKET'
options results
address value client action server
if rc = 0 then say result
else say 'Connection command unavailable, RC' rc
