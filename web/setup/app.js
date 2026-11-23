var $ = function (id) {
    return document.getElementById(id);
};
var timer = null;

function bars(r) {
    return r > -55 ? '▂▄▆█' : r > -67 ? '▂▄▆' : r > -78 ? '▂▄' : '▂';
}

function select(btn, n) {
    var all = document.querySelectorAll('.net');
    for (var i = 0; i < all.length; i++) all[i].classList.remove('sel');
    btn.classList.add('sel');
    $('ssid').value = n.ssid;
    if (!n.secured) $('pass').value = '';
    $('pass').focus();
}

function render(list) {
    var box = $('nets');
    box.replaceChildren();
    list.forEach(function (n) {
        var b = document.createElement('button');
        b.type = 'button';
        b.className = 'net';
        var name = document.createElement('span');
        name.className = 'name';
        name.textContent = n.ssid;
        var meta = document.createElement('span');
        meta.className = 'meta';
        meta.textContent = (n.secured ? '🔒 ' : 'open ') + bars(n.rssi);
        meta.title = n.rssi + ' dBm';
        b.appendChild(name);
        b.appendChild(meta);
        if (n.ssid === $('ssid').value) b.classList.add('sel');
        b.onclick = function () {
            select(b, n);
        };
        box.appendChild(b);
    });
}

function scan(fresh) {
    clearTimeout(timer);
    $('scanmsg').textContent = 'Scanning...';
    $('refresh').disabled = true;
    fetch('/scan', { method: fresh ? 'POST' : 'GET' })
        .then(function (r) {
            return r.json();
        })
        .then(function (d) {
            render(d.networks || []);
            if (d.scanning) {
                timer = setTimeout(function () {
                    scan(false);
                }, 1500);
                return;
            }
            $('refresh').disabled = false;
            $('scanmsg').textContent = d.networks && d.networks.length ? '' : 'No networks found.';
        })
        .catch(function () {
            $('refresh').disabled = false;
            $('scanmsg').textContent = 'Could not reach the generator.';
        });
}

function passOk(p) {
    return p.length === 0 || (p.length >= 8 && p.length <= 63) || /^[0-9a-fA-F]{64}$/.test(p);
}

function msg(text, cls) {
    var m = $('msg');
    m.textContent = text;
    m.className = cls || '';
}

$('refresh').onclick = function () {
    scan(true);
};
$('show').onchange = function () {
    $('pass').type = this.checked ? 'text' : 'password';
};

$('form').onsubmit = function (e) {
    e.preventDefault();
    var ssid = $('ssid').value,
        pass = $('pass').value;
    if (!ssid) {
        msg('Enter a network name.', 'err');
        return;
    }
    if (new TextEncoder().encode(ssid).length > 32) {
        msg('The network name is longer than 32 bytes.', 'err');
        return;
    }
    if (!passOk(pass)) {
        msg('The password must be 8-63 characters (or empty for an open network).', 'err');
        return;
    }
    $('save').disabled = true;
    msg('Saving...');
    fetch('/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid: ssid, password: pass }),
    })
        .then(function (r) {
            return r.json().then(function (d) {
                return { ok: r.ok, d: d };
            });
        })
        .then(function (res) {
            if (!res.ok) {
                $('save').disabled = false;
                msg(res.d.error || 'Saving failed.', 'err');
                return;
            }
            clearTimeout(timer);
            $('donessid').textContent = ssid;
            $('pick').hidden = true;
            $('form').hidden = true;
            $('done').hidden = false;
        })
        .catch(function () {
            $('save').disabled = false;
            msg('Could not reach the generator.', 'err');
        });
};

scan(false);
