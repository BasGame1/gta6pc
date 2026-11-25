GRAD_R=(255 242 145 20)
GRAD_G=(195 92  85  120)
GRAD_B=(113 162 245 255)
_print_gradient() {
    local text="$1"
    local len="${#text}"
    local ref_len="${2:-$len}"

    if (( len == 0 )); then
        return
    fi

    local denom=$(( ref_len > 1 ? ref_len - 1 : 1 ))
    local i char pos seg t r g b

    for (( i = 0; i < len; i++ )); do
        char="${text:i:1}"
        if [[ "$char" == " " ]]; then
            printf ' '
            continue
        fi

        pos=$(( i * 3000 / denom ))
        seg=$(( pos / 1000 ))
        t=$(( pos % 1000 ))

        if (( seg >= 3 )); then
            seg=2
            t=1000
        fi

        r=$(( GRAD_R[seg] + (GRAD_R[seg+1] - GRAD_R[seg]) * t / 1000 ))
        g=$(( GRAD_G[seg] + (GRAD_G[seg+1] - GRAD_G[seg]) * t / 1000 ))
        b=$(( GRAD_B[seg] + (GRAD_B[seg+1] - GRAD_B[seg]) * t / 1000 ))

        printf '\e[1;38;2;%d;%d;%dm%s' "$r" "$g" "$b" "$char"
    done
    printf '\e[0m'
}

banner() {
    local lines
    mapfile -t lines << 'EOF'
  _____   _______      ___      _____    _____     _____ 
 / ____| |__   __|    /   \    |___  \  |  __ \   / ____|
| |  __     | |      /  ^  \      / /   | |__) | | |     
| | |_ |    | |     /  /_\  \    / /    |  ___/  | |     
| |__| |    | |    /  _____  \  / /___  | |      | |____ 
 \_____|    |_|   /_/       \_\|______| |_|       \_____|
                  |_|       |_|                          
EOF

    local max_len=0
    local line
    for line in "${lines[@]}"; do
        if (( ${#line} > max_len )); then
            max_len=${#line}
        fi
    done

    for line in "${lines[@]}"; do
        _print_gradient "$line" "$max_len"
        printf '\n'
    done
}

error() {
    printf '\e[1;38;2;255;65;65mERROR:\e[0m %s\n' "$*"
}

info() {
    _print_gradient "INFO:"
    printf ' %s\n' "$*"
}

success() {
    printf '\e[1;38;2;50;220;100mSUCCESSFUL:\e[0m %s\n' "$*"
}

printc() {
    _print_gradient "$*"
    printf '\n'
}
