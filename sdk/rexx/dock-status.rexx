/* Read-only dock snapshot. Optional argument: AMIROCKET.2 */
parse arg client
if client = '' then client = 'AMIROCKET'
options results
address value client 'DOCKSTATUS'
if rc = 0 then say result
else say 'AmiRocket snapshot unavailable, RC' rc
