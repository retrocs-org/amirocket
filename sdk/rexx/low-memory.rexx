/* Temporary settings; add address AMIROCKET 'SAVE' only to persist them. */
options results
address AMIROCKET 'SETCONFIG show_avatars 0'
address AMIROCKET 'SETCONFIG show_emoji 0'
address AMIROCKET 'SETCONFIG inline_images 0'
address AMIROCKET 'SETCONFIG interval 30'
address AMIROCKET 'STATUS'
if rc = 0 then say result
