let current_key = new Map()
let password_set = false
let stored_password = []
let password = []

function avg(a, b) {
    return (a + b) / 2
}

function parse_password() {
    if (password.length < 1) return []
    // pw structure = [keycode, dwell time, flight time]
    pw = [[password[0][0], password[0][2] - password[0][1], 0]]
    for (let i = 1; i < password.length; i++) {
        pw.push([password[i][0], password[i][2] - password[i][1], password[i][1] - password[i-1][1]])
    }
    pw[pw.length-1][2] = 0
    return pw
}

function check_password(threshold = 11) {
    pw = parse_password()
    
    if (pw.length != stored_password.length) {
        console.log("Do you even know the password")
        return false
    }
    
    total_deviation = 0.0
    for (let i = 0; i < pw.length; i++) {
        if (pw[i][0] != stored_password[i][0]) return false
        total_deviation += Math.abs(pw[i][1] - stored_password[i][1]) // dwell difference
        total_deviation += Math.abs(pw[i][2] - stored_password[i][2]) // flight difference
    }
    total_deviation /= pw.length * 2
    console.log(threshold)
    console.log(total_deviation)
    if (total_deviation <= threshold) {
        // stored password should 'evolve' with user
        // weighted in favor of stored_password
        for (let i = 0; i < stored_password.length; i++) {
            stored_password[i][1] = (pw[i][1] + stored_password[i][1] * 3) / 4
            stored_password[i][2] = (pw[i][2] + stored_password[i][2] * 3) / 4
        }
        return true
    }
    return false
}

document.addEventListener("DOMContentLoaded", () => {
    const pw_field = document.getElementById("pw")
    const pw_container = document.getElementById("pw-container")
    if (localStorage.getItem("pw")) {
        stored_password = localStorage.getItem("pw")
        password_set = true
    }
    window.addEventListener("keydown", (event) => {
        if (event.isTrusted) {
            if (!event.shiftKey && !event.metaKey && !event.altKey && !event.ctrlKey && !event.repeat && event.code != "Escape") {
                current_key.set(event.code, event.timeStamp)
            }
        }
    })
    window.addEventListener("keyup", (event) => {
        if (event.isTrusted) {
            if (!event.shiftKey && !event.metaKey && !event.altKey && !event.ctrlKey && !event.repeat && event.code != "Escape") {
                if (event.code == "Enter") {
                    if (!password_set) {
                        stored_password = parse_password()
                        password_set = true
                        pw_container.style.borderColor = "#4fe966"
                        pw_container.style.boxShadow = "0 0 10px #4fe966"
                        pw_field.innerHTML = "Stored"
                    } else {
                        if (check_password()) {
                            pw_container.style.borderColor = "#4fe966"
                            pw_container.style.boxShadow = "0 0 10px #4fe966"
                            pw_field.innerHTML = "Authenticated"
                        } else {
                            pw_container.style.borderColor = "#e94f4f"
                            pw_container.style.boxShadow = "0 0 10px #e94f4f"
                            pw_field.innerHTML = "Try again"

                        }
                    }
                    password = []
                    current_key.clear()
                    setTimeout(() => {
                        pw_container.style.borderColor = "#4fd7e9"
                        pw_container.style.boxShadow = "0 0 10px #4fd7e9"
                        pw_field.innerHTML = "Enter password"
                    }, 1000)
                } 
                else if (event.code == "Backspace") {
                    password = []
                    current_key.clear()
                    pw_container.style.borderColor = "#4fd7e9"
                    pw_container.style.boxShadow = "0 0 10px #4fd7e9"
                    pw_field.innerHTML = "Enter password"
                }
                else if (current_key.get(event.code)) {
                    password.push([event.code, current_key.get(event.code), event.timeStamp])
                    pw_container.style.borderColor = "#4fd7e9"
                    pw_container.style.boxShadow = "0 0 10px #4fd7e9"
                    pw_field.innerHTML = "•".repeat(password.length)
                }
            }
        }
    })
})

