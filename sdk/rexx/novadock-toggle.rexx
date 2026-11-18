/* Assign this script to a NovaDock action to show AmiRocket. */
options results
address AMIROCKET 'SHOW'
address AMIROCKET 'STATUS'
if rc = 0 then say result
