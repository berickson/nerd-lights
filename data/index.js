var the_element;
customElements.define('color-button',
    class extends HTMLElement {
        constructor() {
            super();
            this.template = document.getElementById('color-button');
            the_element = this;
        }

        // using connected compenent instead of shadow dom so styles can be inheritd
        connectedCallback() {
            let e=document.importNode(this.template.content, true);
            this.appendChild(e);
            let r = this.getAttribute('r');
            let g = this.getAttribute('g');
            let b = this.getAttribute('b');
            let method = this.getAttribute('method');


            // get full colors to use on screen
            let v = Math.max(r,g,b)
            let screen_r = v == 0 ? r : r*255/v;
            let screen_g = v == 0 ? g : g*255/v;
            let screen_b = v == 0 ? b : b*255/v;
            let screen_color = 'rgb('+screen_r+","+screen_g+","+screen_b+')';
            this.querySelector("#color-box").style.fill=screen_color;
            let t = this.innerText;
            this.querySelector("#label").innerText = this.getAttribute('name');
            //this.innerText = "";
            
            this.querySelector("#color-box").addEventListener("mousedown", function() {
                let color = String(r)+" "+g+" "+b;
                if(method=="add") {
                    command("add " + color);
                } else {
                    command("color " + color);
                }
            });
            // e.addEventListener("mousedown", function() {
            //     set_color(String(r)+" "+g+" "+b);
            // });

        }
    }
);

function log_scale(p, minp=0, maxp=100, min=0.1, max=10) {
    // The result should be between min and max
    var minv = Math.log(min);
    var maxv = Math.log(max);

    // calculate adjustment factor
    var scale = (maxv-minv) / (maxp-minp);

    return Math.exp(minv + scale*(p-minp));
}

function set_color(c) {
    let add_checked = document.getElementById('add_checkbox').checked;
    let cmd = (add_checked ? "add " : "color ") + c;
    command(cmd);
}

function command(s) {
    let r = new XMLHttpRequest();
    r.open("POST", "command");
    r.setRequestHeader("Content-Type","application/x-www-form-urlencoded; charset=UTF-8");
    r.send(s);
}
function power_on() {
    command("on");
}

function power_off() {
    command("off");
}

function cycles_input() {
    let cycles_p = parseInt(document.getElementById("cycles").value);
    let cycles = log_scale(cycles_p, 1, 100, 0.01, 10);
    let cycles_str = cycles.toFixed(2);
    document.getElementById('cycles_value').innerHTML = cycles_str;
}

function cycles_change() {
    let v = document.getElementById('cycles_value').innerHTML
    command("cycles "+ v);
}

function speed_input() {
    let speed_p = parseInt(document.getElementById("speed").value);
    let speed = log_scale(speed_p, 1, 100, 0.01, 10);
    let speed_str = speed.toFixed(2);
    document.getElementById('speed_value').innerHTML = speed_str;
}

function speed_change() {
    let v = document.getElementById('speed_value').innerHTML
    command("speed "+ v);
}

function brightness_input() {
    let brightness = parseInt(document.getElementById("brightness").value);
    let brightness_str = String(brightness);
    document.getElementById('brightness_value').innerHTML = brightness_str;
}

function brightness_change() {
    let v = document.getElementById('brightness_value').innerHTML
    command("brightness "+ v);
}

function saturation_input() {
    let saturation = parseInt(document.getElementById("saturation").value);
    let saturation_str = String(saturation);
    document.getElementById('saturation_value').innerHTML = saturation_str;
}

function saturation_change() {
    let v = document.getElementById('saturation_value').innerHTML
    command("saturation "+ v);
}
