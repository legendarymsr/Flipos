# Remove flipos entirely. Stops the VM, then deletes /data/flipos (including
# the guest's data.img). Only ever touches /data/flipos.
[ -f /data/flipos/bin/flipos ] && sh /data/flipos/bin/flipos stop >/dev/null 2>&1
rm -rf /data/flipos
