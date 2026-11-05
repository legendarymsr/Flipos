#!/system/bin/sh
# KernelSU "Action" button: toggles flipos. Start opens the viewer in your browser.
F=/data/flipos
if [ -f "$F/run/qemu.pid" ] && kill -0 "$(cat "$F/run/qemu.pid")" 2>/dev/null; then
  echo "flipos is running — stopping it."
  sh "$F/bin/flipos" stop
else
  sh "$F/bin/flipos" start || exit 1
  URL="$(sh "$F/bin/flipos" url | sed -n 's/.*browser: //p')"
  echo "Opening the viewer in your browser (wait ~20 s for the lock screen)…"
  am start -a android.intent.action.VIEW -d "$URL" >/dev/null 2>&1 || true
fi
echo "Press Action again to stop."
