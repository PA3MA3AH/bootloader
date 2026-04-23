#!/usr/bin/env bash
set -e

TAP=tap0
HOST_IP=192.168.100.1/24
NET=192.168.100.0/24

# Можно переопределить через env: EXT_IF=wlp3s0 sudo ./tap-setup.sh
# Иначе берём интерфейс из маршрута по умолчанию.
EXT_IF="${EXT_IF:-$(ip -4 route show default | awk '/default/ {print $5; exit}')}"
USER_NAME="${SUDO_USER:-$USER}"

if [ -z "$EXT_IF" ]; then
    echo "[tap-setup] ERROR: не удалось определить внешний интерфейс" >&2
    exit 1
fi

# создать tap0, если ещё нет
if ! ip link show "$TAP" >/dev/null 2>&1; then
    ip tuntap add dev "$TAP" mode tap user "$USER_NAME"
fi

# поднять tap0
ip link set "$TAP" up

# назначить IP, если ещё не назначен
if ! ip addr show "$TAP" | grep -q "${HOST_IP%/*}"; then
    ip addr add "$HOST_IP" dev "$TAP"
fi

# включить форвардинг ядра
sysctl -wq net.ipv4.ip_forward=1

# NAT-правило, идемпотентно
if ! iptables -t nat -C POSTROUTING -s "$NET" -o "$EXT_IF" -j MASQUERADE 2>/dev/null; then
    iptables -t nat -A POSTROUTING -s "$NET" -o "$EXT_IF" -j MASQUERADE
fi
if ! iptables -C FORWARD -i "$TAP" -o "$EXT_IF" -j ACCEPT 2>/dev/null; then
    iptables -A FORWARD -i "$TAP" -o "$EXT_IF" -j ACCEPT
fi
if ! iptables -C FORWARD -i "$EXT_IF" -o "$TAP" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null; then
    iptables -A FORWARD -i "$EXT_IF" -o "$TAP" -m state --state RELATED,ESTABLISHED -j ACCEPT
fi

echo "[tap-setup] ok: $TAP up, NAT via $EXT_IF (user=$USER_NAME)"
