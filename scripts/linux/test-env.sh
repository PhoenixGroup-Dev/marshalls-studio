#!/usr/bin/env bash
# Starts a self-contained environment for the engine integration tests:
#   * Xvfb display :99 (OpenGL through Mesa llvmpipe)
#   * PulseAudio with two independent virtual microphones (440 Hz / 880 Hz
#     sine sources) and a null sink used as monitoring output
#   * nginx-rtmp on 127.0.0.1:1935, application "live", recording every
#     received stream to $MS_TEST_ENV/rtmp
# Usage: source scripts/linux/test-env.sh start|stop
set -u
MS_TEST_ENV="${MS_TEST_ENV:-/tmp/ms-test-env}"
export MS_TEST_ENV
cmd="${1:-start}"

start() {
	mkdir -p "$MS_TEST_ENV/rtmp" "$MS_TEST_ENV/nginx"
	chmod 1777 "$MS_TEST_ENV/rtmp" # nginx workers run as www-data
	if ! pgrep -f "Xvfb :99" >/dev/null; then
		setsid nohup Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp >"$MS_TEST_ENV/xvfb.log" 2>&1 < /dev/null &
		sleep 1
	fi
	export DISPLAY=:99
	xsetroot -solid "#2a5599" 2>/dev/null || true

	if ! pactl info >/dev/null 2>&1; then
		pulseaudio --daemonize=yes --exit-idle-time=-1 --log-target=file:"$MS_TEST_ENV/pulse.log" 2>/dev/null
		for _ in $(seq 1 20); do pactl info >/dev/null 2>&1 && break; sleep 0.2; done
	fi
	# Two independent "microphones": a tone played in real time (10 ms packets,
	# like real capture hardware) into a null sink, exposed as a capture source.
	for spec in "a:440" "b:880"; do
		n="${spec%%:*}"; f="${spec##*:}"
		pactl list short sinks | grep -q "ms_play_$n" || pactl load-module module-null-sink sink_name="ms_play_$n" rate=48000 >/dev/null
		pactl list short sources | grep -q "ms_mic_$n\b" || pactl load-module module-remap-source master="ms_play_$n.monitor" source_name="ms_mic_$n" >/dev/null
		if ! pgrep -f "^ffmpeg .*ms-tone-$n" >/dev/null; then
			setsid nohup ffmpeg -nostdin -loglevel error -re -f lavfi -i "sine=f=$f:sample_rate=48000" -af volume=0.5 \
				-f pulse -device "ms_play_$n" "ms-tone-$n" >"$MS_TEST_ENV/tone-$n.log" 2>&1 </dev/null &
		fi
	done
	# Two more inputs fed by ONE beep generator (same clock): used to measure
	# per-input audio delay. 50 ms 1 kHz beep every second.
	for n in c d; do
		pactl list short sinks | grep -q "ms_play_$n" || pactl load-module module-null-sink sink_name="ms_play_$n" rate=48000 >/dev/null
		pactl list short sources | grep -q "ms_mic_$n\b" || pactl load-module module-remap-source master="ms_play_$n.monitor" source_name="ms_mic_$n" >/dev/null
	done
	if ! pgrep -f "^ffmpeg .*ms-beeps" >/dev/null; then
		setsid nohup ffmpeg -nostdin -loglevel error -re -f lavfi -i "sine=f=1000:sample_rate=48000" \
			-filter_complex "[0:a]volume=0.5,volume=0:enable='gte(mod(t,1),0.05)',asplit=2[c][d]" \
			-map "[c]" -f pulse -device ms_play_c "ms-beeps-c" -map "[d]" -f pulse -device ms_play_d "ms-beeps-d" \
			>"$MS_TEST_ENV/beeps.log" 2>&1 </dev/null &
	fi
	pactl list short sinks | grep -q ms_monitor || pactl load-module module-null-sink sink_name=ms_monitor >/dev/null
	pactl set-default-sink ms_monitor >/dev/null 2>&1 || true

	cat >"$MS_TEST_ENV/nginx/nginx.conf" <<CONF
load_module /usr/lib/nginx/modules/ngx_rtmp_module.so;
daemon on;
pid $MS_TEST_ENV/nginx/nginx.pid;
error_log $MS_TEST_ENV/nginx/error.log info;
events { worker_connections 64; }
rtmp {
  server {
    listen 127.0.0.1:1935;
    application live {
      live on;
      record all;
      record_path $MS_TEST_ENV/rtmp;
      record_unique on;
    }
  }
}
CONF
	if ! [ -f "$MS_TEST_ENV/nginx/nginx.pid" ] || ! kill -0 "$(cat "$MS_TEST_ENV/nginx/nginx.pid")" 2>/dev/null; then
		nginx -c "$MS_TEST_ENV/nginx/nginx.conf" -p "$MS_TEST_ENV/nginx" -e "$MS_TEST_ENV/nginx/error.log"
	fi
	echo "test env ready: DISPLAY=$DISPLAY, pulse sources: $(pactl list short sources | awk '{print $2}' | tr '\n' ' ')"
}

stop() {
	[ -f "$MS_TEST_ENV/nginx/nginx.pid" ] && kill "$(cat "$MS_TEST_ENV/nginx/nginx.pid")" 2>/dev/null
	pkill -f "ms-tone-" 2>/dev/null
	pkill -f "ms-beeps" 2>/dev/null
	pulseaudio --kill 2>/dev/null
	pkill -f "Xvfb :99" 2>/dev/null
	true
}

"$cmd"
