let is_touch_device = 'ontouchstart' in document.documentElement;

customElements.define('color-button',
    class extends HTMLElement {
        constructor() {
            super();
            this.template = document.getElementById('color-button');
        }

        // using connected compenent instead of shadow dom so styles can be inheritd
        connectedCallback() {
            let me = this;
            let e=document.importNode(this.template.content, true);
            this.appendChild(e);
            let r = this.getAttribute('r');
            let g = this.getAttribute('g');
            let b = this.getAttribute('b');
            let method = this.getAttribute('method');



            let rgb_color = 'rgb('+r+","+g+","+b+')';
            this.querySelector("#color-box").style.fill=rgb_color;
            let t = this.innerText;
            this.querySelector("#label").innerText = this.getAttribute('name');
            //this.innerText = "";
            
            let press_timer = null;

            function on_down() {
                if(press_timer!=null) {
                    return;
                }
                let color = String(r)+" "+g+" "+b;
                if(method=="add") {
                    command("add " + color, update_status);

                    press_timer = setTimeout(function() { 
                        command("color " + color, update_status);
                        console.log('timout fired');
                        press_timer = null;
                    }, 1000);
                }
                if(method=="pick") {
                    color_picker.color.rgbString = rgb_color;
                    let modal = document.getElementById("picker-modal-dialog");
                    modal.style.display = "block"; // show color picker
                    let color_box = me.querySelector("#color-box");
                    color_picker.off('color:change');
                    color_picker.on('color:change', function(color) {
                        color_box.style.fill=color.hexString;
                        me.setAttribute('r', color.rgb.r)
                        me.setAttribute('g', color.rgb.g)
                        me.setAttribute('b', color.rgb.b)
                      });

                    // When the user clicks anywhere outside of the modal, close it
                    window.addEventListener(is_touch_device ? "touchstart" : "mousedown", 
                        function (event) {
                            if (event.target == modal) {
                                modal.style.display = "none";
                                color_picker.off('color:change');
                            }
                        }
                    )
                }
            }

            function on_up() {
                if(press_timer != null) {
                    clearTimeout(press_timer);
                    press_timer = null;
                    console.log('timout cleared');
                }
            }




            this.querySelector("#color-box").addEventListener(is_touch_device ? "touchstart" : "mousedown", on_down);
            this.querySelector("#color-box").addEventListener(is_touch_device ? "touchend" : "mouseup", on_up);



            // e.addEventListener("mousedown", function() {
            //     set_color(String(r)+" "+g+" "+b);
            // });

        }
    }
);

function log_scale(p, minp=0, maxp=100, min=0.1, max=10) {
    // The result should be between min and max
    let minv = Math.log(min);
    let maxv = Math.log(max);

    // calculate adjustment factor
    let scale = (maxv-minv) / (maxp-minp);

    return Math.exp(minv + scale*(p-minp));
}

function inverse_log_scale(v, minp=0, maxp=100, min=0.1, max=10) {
    // The result should be between min and max
    let minv = Math.log(min);
    let maxv = Math.log(max);

    // calculate adjustment factor
    let scale = (maxv-minv) / (maxp-minp);
    let p = (Math.log(v)-minv)/scale + minp;
    return p
}

function set_color(c) {
    let add_checked = document.getElementById('add_checkbox').checked;
    let cmd = (add_checked ? "add " : "color ") + c;
    command(cmd);
}

function command(s, onload = null) {
    let r = new XMLHttpRequest();
    r.open("POST", "command");
    r.setRequestHeader("Content-Type","application/x-www-form-urlencoded; charset=UTF-8");
    if(onload!=null) {
        r.onload = onload;
    }
    r.send(s);
}

function multi_command(commands, onload = null) {
    let s = commands[0];
    let r = new XMLHttpRequest();
    r.open("POST", "command")
    r.setRequestHeader("Content-Type","application/x-www-form-urlencoded; charset=UTF-8");
    r.onload = function (e) {
        if (r.readyState === 4) {
          if (r.status === 200) {
            let remaining_commands=commands.slice(1);
            if(remaining_commands.length > 0) {
                multi_command(remaining_commands);
            } else {
                update_status();
            }
          } else {
            alert(r.statusText);
          }
        }
      };    
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

function update_cycles(v) {
    document.getElementById("cycles").value = inverse_log_scale(v, 1, 100, 0.01, 10);
    document.getElementById('cycles_value').innerHTML = v;
}

function speed_input() {
    let speed_p = parseInt(document.getElementById("speed").value);
    let speed = (speed_p == 0 )? 0 : log_scale(speed_p, 1, 100, 0.001, 1);
    let speed_str = speed.toFixed(3);
    document.getElementById('speed_value').innerHTML = speed_str;
}

function speed_change() {
    let v = document.getElementById('speed_value').innerHTML
    command("speed "+ v);
}

function update_speed(v) {
    document.getElementById('speed_value').innerHTML = v.toString();
    document.getElementById("speed").value = (v==0) ? 0 : inverse_log_scale(v, 1, 100, 0.001, 1);
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

function update_brightness(v) {
    document.getElementById("brightness").value
    document.getElementById('brightness_value').innerHTML = v.toString();
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

function update_saturation(v) {
    document.getElementById("saturation").value = v
    document.getElementById('saturation_value').innerHTML = v.toString();
}



function on_status_update(e) {
    let status = JSON.parse(e.target.response);
    let colors = status.colors;
    let colors_div = document.getElementById("current-colors");
    colors_div.innerHTML = "";
    for(let i=0; i<colors.length; ++i) {
        let color = colors[i];
        let button = document.createElement("color-button");
        button.setAttribute("r",color.r)
        button.setAttribute("g",color.g)
        button.setAttribute("b",color.b)
        button.setAttribute("method", "pick")
        //button.setAttribute("name",i.toString())
        button.name = i.toString();
        colors_div.appendChild(button);
    }
    update_speed(status.speed);
    update_brightness(status.brightness);
    update_cycles(status.cycles);
    update_saturation(status.saturation);
}

function update_status() {
    command("status", on_status_update);
}

function on_network_connect_click() {
    let ssid = get_selected_ssid();
    let password = document.getElementById('network_password').value;
    command('set_wifi_config '+ssid+' '+password);
}

function scan_networks() {
    let r = new XMLHttpRequest();
    r.open("POST", "command");
    r.setRequestHeader("Content-Type","application/x-www-form-urlencoded; charset=UTF-8");
    r.onload = function (e) {
        let networks = JSON.parse(r.response).networks
        populate_network_list(networks);
    };
    r.send("scan_networks");
    
}

function populate_network_list(networks) {
    let select_element = document.createElement('select')
    select_element.id = 'network_select'
    let added = new Set()
  
    for(let i=0;i<networks.length; ++i) {
      let ssid = networks[i].ssid;
      if(added.has(ssid)) continue;
      added.add(ssid)
      let option_element = document.createElement('option');
      option_element.value = ssid;
      option_element.innerText = ssid;
      select_element.appendChild(option_element);
    }
    document.getElementById("networks").appendChild(select_element)
  }
  
function get_selected_ssid() {
    // get ssid
    let ssid = "";
    if( document.getElementById("picker_button").checked ) {
      let e = document.getElementById('network_select');
      ssid = e.options[e.selectedIndex].value;
    } else {
      ssid = document.getElementById('custom_value').value;
    }
    return ssid;
}
  