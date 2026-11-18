/* Query a running client without touching its draft or network session. */
options results
address AMIROCKET 'STATUS'
if rc = 0 then say result
else say 'AmiRocket is not available, RC='rc
