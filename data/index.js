let is_touch_device = 'ontouchstart' in document.documentElement;
let globals = {};
globals.device_name = "?";
globals.devices = [];

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
            let color_pending = false;



            let rgb_string = 'rgb('+r+","+g+","+b+')';
            this.querySelector("#color-box").style.fill=rgb_string;
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
                    color_picker.color.rgbString = rgb_string;
                    let modal = document.getElementById("picker-modal-dialog");
                    modal.style.display = "block"; // show color picker
                    let color_box = me.querySelector("#color-box");

                    // the input:end event is called when acolor is picked
                    // tried color:change before, but frequent updates caused 
                    // too much flickering
                    color_picker.off('input:end'); 
                    color_picker.on('input:end', function(color) {
                        rgb_string = color.rgbString;
                        color_box.style.fill=rgb_string;
                        me.setAttribute('r', color.rgb.r)
                        me.setAttribute('g', color.rgb.g)
                        me.setAttribute('b', color.rgb.b)

                        let buttons = document.getElementById('current-colors').querySelectorAll('color-button');
                        let command_string = "color";
                        for(let i = 0; i < buttons.length; ++i) {
                            let button = buttons[i];
                            let r = button.getAttribute('r')
                            let g = button.getAttribute('g')
                            let b = button.getAttribute('b')
                            command_string += " "+r+" "+g+" "+b;
                        }
                        if(color_pending == false) {
                            color_pending = true;
                            command(command_string, function () {color_pending = false});
                        }
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

function show_send_to_dialog() {
    let modal = document.getElementById("send-to-dialog");
    modal.style.display = "block"; // show color picker

    // update the devices list
    let devices_div = document.getElementById("send-to-devices");
    devices_div.innerHTML = "";
    for(const device of globals.devices) {
        let button = document.createElement("button");
        button.innerText = device.hostname;
        button.onclick = function() {
            command("get_program", function(e) {
                let command = "set_program "+e.target.response;
                remote_command(device.ip_address, command);
                alert('sent ' + command + ' to '+device.ip_address);

            });
        }
        devices_div.appendChild(button);
    }

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


function remote_command(host, s, onload = null) {
    let r = new XMLHttpRequest();
    r.open("POST", "http://"+host+"/command");
    r.setRequestHeader("Content-Type","application/x-www-form-urlencoded; charset=UTF-8");
    if(onload!=null) {
        r.onload = onload;
    }
    r.send(s);
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

function isAlphaNumeric(str) {
    var code, i, len;
  
    for (i = 0, len = str.length; i < len; i++) {
      code = str.charCodeAt(i);
      if (!(code > 47 && code < 58) && // numeric (0-9)
          !(code > 64 && code < 91) && // upper alpha (A-Z)
          !(code > 96 && code < 123)) { // lower alpha (a-z)
        return false;
      }
    }
    return true;
  };
  
function is_mobile_device() {
    let rv  = /Mobi|Android/i.test(navigator.userAgent);
    return rv;
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
    command("on", update_status);
}

function power_off() {
    command("off", update_status);
}

function cycles_input() {
    let cycles_p = parseInt(document.getElementById("cycles").value);
    let cycles = log_scale(cycles_p, 1, 100, 0.01, 10);
    let cycles_str = cycles.toFixed(2);
    document.getElementById('cycles_value').innerText = cycles_str;
    document.getElementById('cycles_value').setAttribute("class", "medium");
}

function cycles_change() {
    let v = document.getElementById('cycles_value').innerText
    command("cycles "+ v);
}

function update_cycles(v) {
    document.getElementById("cycles").value = inverse_log_scale(v, 1, 100, 0.01, 10);
    document.getElementById('cycles_value').innerText = v.toFixed(2);
}

function speed_input() {
    let speed_p = parseInt(document.getElementById("speed").value);
    let speed = (speed_p == 0 )? 0 : log_scale(speed_p, 1, 100, 0.001, 1);
    let speed_str = speed.toFixed(3);
    document.getElementById('speed_value').innerText = speed_str;
}

function speed_change() {
    let v = document.getElementById('speed_value').innerText
    command("speed "+ v);
}

function update_speed(v) {
    document.getElementById('speed_value').innerText = v.toString();
    document.getElementById("speed").value = (v==0) ? 0 : inverse_log_scale(v, 1, 100, 0.001, 1);
}

function brightness_input() {
    let brightness = parseInt(document.getElementById("brightness").value);
    let brightness_str = String(brightness);
    document.getElementById('brightness_value').innerText = brightness_str;
}

function brightness_change() {
    let v = document.getElementById('brightness_value').innerText;
    command("brightness "+ v);
}

function update_brightness(v) {
    document.getElementById("brightness").value
    document.getElementById('brightness_value').innerText = v.toString();
}

function saturation_input() {
    let saturation = parseInt(document.getElementById("saturation").value);
    let saturation_str = String(saturation);
    document.getElementById('saturation_value').innerText = saturation_str;
}

function saturation_change() {
    let v = document.getElementById('saturation_value').innerText
    command("saturation "+ v);
}

function update_saturation(v) {
    document.getElementById("saturation").value = v
    document.getElementById('saturation_value').innerText = v.toString();
}

function compare_strings(a,b) {
    if(a==b) return 0;
    if(a<b) return -1;
    return 1;
}

function on_devices_update(e) {
    let response = JSON.parse(e.target.response);
    if(response.success) {
        let div = document.getElementById("devices_div");
        div.innerHTML="";
        globals.devices = response.devices;

        // all_devices is this device plus other devices
        let all_devices = globals.devices.slice();
        all_devices.push({"hostname":globals.device_name});
        all_devices.sort((a,b)=> compare_strings(a.hostname.toLowerCase(),b.hostname.toLowerCase()));

        for(const device of all_devices) {
            let button = document.createElement("button");
            if("ip_address" in device) {
                button.setAttribute("onmousedown", "window.location.href='"+"http://"+device.ip_address+"'");
            } else {
                // this device should show as active (halo)
                button.setAttribute("active",true); 
            }
            button.innerText = device.hostname;
            div.appendChild(button);
        }
    }
 
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
    globals.device_name = status.device_name;

    document.getElementById("page_title").innerText = "Nerd Lights: " + status.device_name;
    document.getElementById("head_title").innerText = status.device_name;
    document.getElementById("on_button").setAttribute("active", status.lights_on);
    document.getElementById("off_button").setAttribute("active", !status.lights_on);
    document.getElementById("normal_button").setAttribute("active", status.light_mode == "normal");
    document.getElementById("gradient_button").setAttribute("active", status.light_mode == "gradient");
    document.getElementById("meteor_button").setAttribute("active", status.light_mode == "meteor");
    document.getElementById("rainbow_button").setAttribute("active", status.light_mode == "rainbow");
    document.getElementById("twinkle_button").setAttribute("active", status.light_mode == "twinkle");
    document.getElementById("pattern1_button").setAttribute("active", status.light_mode == "pattern1");
    document.getElementById("explosion_button").setAttribute("active", status.light_mode == "explosion");
    document.getElementById("stripes_button").setAttribute("active", status.light_mode == "stripes");
    document.getElementById("strobe_button").setAttribute("active", status.light_mode == "strobe");
    document.getElementById("flicker_button").setAttribute("active", status.light_mode == "flicker");

}

function update_devices() {
    command("get_nearby_devices", on_devices_update);
}

function update_status() {
    command("status", on_status_update);
}

function on_network_connect_click() {
    let ssid = get_selected_ssid();
    let password = document.getElementById('network_password').value;
    command('set_wifi_config '+ssid+' '+password);
    window.alert("Your WiFi configuration as been sent to your Nerd Lights and they will now connect to internet. You can reconnect to your home WiFi and then browse to the address shown your Nerd Light's display");
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
  
