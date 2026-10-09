"""Restart contracts: VIESSMANN_WEBSERVER (host), VIESSMANN_RESTART_IMAGE (Docker)."""

import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request


class RestartRequests:
    def request(self, path, body=None, method=None, headers=None):
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}{path}", data=body, method=method,
            headers=headers if headers is not None else {"Content-Type": "application/json"},
        )
        try:
            response = urllib.request.urlopen(request, timeout=2)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            self.assertNotIn("Access-Control-Allow-Origin", response.headers)
            return response.status, response.read()

    def wait_for_health(self, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                if self.request("/health")[0] == 200:
                    return
            except OSError:
                pass
            time.sleep(0.05)
        self.fail("Webserver did not become healthy")

    def assert_invalid_requests(self):
        for method in ["GET", "PUT", "DELETE", "OPTIONS", "HEAD"]:
            with self.subTest(method=method):
                self.assertEqual(self.request("/api/restart", method=method)[0], 405)
        for body in [
            b"", b"{}", b"[]", b'{"confirm":false}', b'{"confirm":1}',
            b'{"confirm":"true"}', b'{"confirm":true,"other":1}',
            b'{"confirm":true,"confirm":true}', b'{"confirm":true,}',
            b'{"confirm":true}junk', b'{"con firm":true}', b'{"confirm":tru e}',
            b'{"confirm":true}\x00', b'{"confirm":true}{}',
        ]:
            with self.subTest(body=body):
                self.assertEqual(self.request("/api/restart", body, "POST")[0], 400)
        self.assertEqual(self.request("/api/restart", b"x" * 257, "POST")[0], 413)
        for content_type in ["text/plain", "application/x-www-form-urlencoded",
                             "multipart/form-data", "application/jsonp"]:
            with self.subTest(content_type=content_type):
                self.assertEqual(self.request(
                    "/api/restart", b'{"confirm":true}', "POST",
                    {"Content-Type": content_type},
                )[0], 415)
        self.assertEqual(self.request("/api/restart", b'{"confirm":true}', "POST", {})[0], 415)
        for extra in [
            {"Sec-Fetch-Site": "cross-site"},
            {"Origin": "https://attacker.example"},
            {"Origin": "null"},
            {"Origin": f"http://127.0.0.1:{self.port}.attacker.example"},
            {"Origin": "https://attacker.example", "Sec-Fetch-Site": "same-site"},
        ]:
            with self.subTest(headers=extra):
                self.assertEqual(self.request(
                    "/api/restart", b'{"confirm":true}', "POST",
                    {"Content-Type": "application/json", **extra},
                )[0], 403)
        self.assertEqual(self.request("/api/system", method="POST")[0], 405)
        self.assertFalse(json.loads(self.request("/api/system")[1])["restart_pending"])


@unittest.skipUnless(os.environ.get("VIESSMANN_WEBSERVER"), "Set VIESSMANN_WEBSERVER")
class HostRestartTests(RestartRequests, unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix=".restart-api-", dir=Path.cwd())
        self.addCleanup(directory.cleanup)
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            self.port = reservation.getsockname()[1]
        self.process = subprocess.Popen(
            [str(Path(os.environ["VIESSMANN_WEBSERVER"]).resolve()),
             "-p", "/nonexistent-restart-test-serial", "-w", str(self.port)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            env={**os.environ, "VIESSMANN_DATA_DIR": directory.name},
        )
        self.addCleanup(self.stop)
        self.wait_for_health()

    def stop(self):
        self.process.terminate()
        self.process.wait(timeout=5)

    def test_host_refuses_restart_and_keeps_serving(self):
        self.assert_invalid_requests()
        system = json.loads(self.request("/api/system")[1])
        self.assertFalse(system["restart_supported"])
        self.assertTrue(system["restart_manager_required"])
        self.assertIsInstance(system["instance_id"], str)
        self.assertTrue(system["instance_id"])
        for headers in [
            {"Content-Type": "application/json"},
            {"Content-Type": "application/json; charset=utf-8",
             "Origin": f"http://127.0.0.1:{self.port}"},
            {"Content-Type": "application/json", "Origin": "https://homeassistant.local",
             "Sec-Fetch-Site": "same-origin"},
        ]:
            self.assertEqual(self.request(
                "/api/restart", b' \n { "confirm" : true } \t', "POST", headers,
            )[0], 503)
        self.assertEqual(self.request("/health")[0], 200)
        self.assertEqual(json.loads(self.request("/api/system")[1])["instance_id"],
                         system["instance_id"])
        self.assertIsNone(self.process.poll())

    def test_shared_ingress_safe_frontend(self):
        for path in ["/", "/settings"]:
            html = self.request(path)[1].decode()
            self.assertIn("Container neu starten", html)
            self.assertIn("window.confirm(", html)
            script = next(script for script in re.findall(
                r"<script>(.*?)</script>", html, re.S | re.I
            ) if "waitForContainer" in script)
            self.assertIn("restartButton.disabled=!system.restart_supported", script)
            self.assertIn("restartFetch('api/restart'", script)
            self.assertIn("JSON.stringify({confirm:true})", script)
            self.assertIn("restartFetch('api/system')", script)
            self.assertIn("system.instance_id!==previousInstance", script)
            self.assertIn("await waitForContainer(previousInstance)", script)
            self.assertIn("new URL('./',window.location.href)", script)
            self.assertNotIn("fetch('/", script)
            if shutil.which("node"):
                subprocess.run(["node", "--check"], input=script, text=True, check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                fixture = r"""
const vm=require('vm'),assert=require('assert'),fs=require('fs');
const script=fs.readFileSync(0,'utf8');
async function scenario(changes,disconnect){
 let now=0,systemCalls=0,handler,accepted=false;
 const button={disabled:true,addEventListener:(event,callback)=>{handler=callback;}};
 const message={textContent:''};
 const location={href:'http://ha.local/api/hassio_ingress/token/settings'};
 const context={URL,AbortController,Date:{now:()=>now},
  window:{location,confirm:()=>true},
  document:{getElementById:id=>id==='restartButton'?button:message},
  setTimeout:(callback,delay)=>{if(delay===250){now+=250;queueMicrotask(callback);}return 1;},
  clearTimeout:()=>{},
  fetch:async path=>{
   if(path==='api/restart'){accepted=true;return {status:202,json:async()=>({status:'restarting'})};}
   assert.equal(path,'api/system');
   if(accepted)systemCalls++;
   if(disconnect&&systemCalls===3)throw new Error('disconnected');
   return {ok:true,json:async()=>({restart_supported:true,restart_pending:false,
    instance_id:changes&&systemCalls>=4?'new-process':'old-process'})};
  }
 };
 vm.runInNewContext(script,context);
 await context.loadRestartCapability();
 await handler();
 if(changes){assert.equal(systemCalls,4);assert.equal(location.href,'http://ha.local/api/hassio_ingress/token/');}
 else{assert.equal(location.href,'http://ha.local/api/hassio_ingress/token/settings');
  assert.ok(message.textContent.includes('manuell'));assert.ok(systemCalls>10);}
 assert.equal(button.disabled,true);
}
(async()=>{await scenario(true,true);await scenario(true,false);await scenario(false,false);})().catch(error=>{console.error(error);process.exitCode=1;});
"""
                result = subprocess.run(["node", "-e", fixture], input=script, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless(shutil.which("node"), "Node required for frontend contract")
    def test_party_command_uses_applied_profile_and_preserves_edits(self):
        html = self.request("/remote")[1].decode()
        self.assertIn("Partytemperatur ist nur", html)
        script = re.search(r"<script>(.*?)</script>", html, re.S | re.I).group(1)
        fixture = r"""
const vm=require('vm'),assert=require('assert'),fs=require('fs');
const script=fs.readFileSync(0,'utf8');
async function scenario(profile){
 const elements=new Map(),posts=[];let finishPost;
 const element=id=>{
  if(!elements.has(id))elements.set(id,{value:'',disabled:false,textContent:'',
   addEventListener:(event,callback)=>{element(id)[event]=callback;}});
  return elements.get(id);
 };
 const data={profile,model:'Vitotrol 300',slot:1,online:true,room_temperature:20,
  desired_room_temperature:21,reduced_room_temperature:17,party_room_temperature:22,
  mode:202,requested_party_mode:false,requested_economy_mode:false,pending_commands:0,
  crc_errors:0,malformed_frames:0,unknown_commands:0,outside_temperature:null,
  heating_enabled:null,datasets:[]};
 const context={document:{getElementById:element,activeElement:null},setTimeout:()=>1,
  fetch:async(path,options={})=>{
   assert.equal(path,'api/remote');
   if(options.method==='POST'){
    posts.push(JSON.parse(options.body));
    return await new Promise(resolve=>{finishPost=()=>resolve({ok:true,json:async()=>({})});});
   }
   return {ok:true,json:async()=>({...data})};
  }};
 vm.runInNewContext(script,context);await context.refresh();
 assert.equal(element('party').disabled,profile==='openv');
 // An edited but unapplied dropdown must not select the command dialect.
 element('profile').value=profile==='wifi'?'openv':'wifi';element('profile').input();
 element('party').value='24';element('party').input();
 element('mode').value='party_on';element('mode').input();
 element('modeForm').onsubmit({preventDefault:()=>{}});
 assert.equal(posts.length,1);assert.equal(posts[0].mode,'party_on');
 if(profile==='wifi')assert.equal(posts[0].party_room_temperature,24);
 else assert.ok(!Object.hasOwn(posts[0],'party_room_temperature'));
 // Edits made while the POST is in flight retain their newer revision.
 element('party').value='25';element('party').input();
 element('mode').value='economy_on';element('mode').input();
 finishPost();for(let i=0;i<20;i++)await Promise.resolve();
 assert.equal(element('party').value,'25');assert.equal(element('mode').value,'economy_on');
 assert.equal(element('profile').value,profile==='wifi'?'openv':'wifi');
 assert.equal(element('party').disabled,profile==='openv');
}
(async()=>{await scenario('wifi');await scenario('openv');})().catch(error=>{console.error(error);process.exitCode=1;});
"""
        result = subprocess.run(["node", "-e", fixture], input=script, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 0, result.stderr)


@unittest.skipUnless(os.environ.get("VIESSMANN_RESTART_IMAGE"), "Set VIESSMANN_RESTART_IMAGE")
class DockerRestartTests(RestartRequests, unittest.TestCase):
    def docker(self, *args):
        return subprocess.check_output(["docker", *args], text=True).strip()

    def wait_for_health(self, timeout=10):
        try:
            super().wait_for_health(timeout)
        except AssertionError as error:
            self.fail(f"{error}\n{self.docker('logs', self.container)}")

    def setUp(self):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            self.port = reservation.getsockname()[1]
        self.container = self.docker(
            "run", "-d", "--init", "--restart", "unless-stopped",
            "-p", f"127.0.0.1:{self.port}:8099", os.environ["VIESSMANN_RESTART_IMAGE"],
        )
        self.addCleanup(self.docker, "rm", "-f", self.container)
        self.wait_for_health(30)

    def test_real_container_restart_and_saved_settings_reload(self):
        self.assert_invalid_requests()
        self.assertTrue(json.loads(self.request("/api/system")[1])["restart_supported"])
        settings = {
            "protocol": "vbus", "baud_rate": 9600, "serial_config": "8N1",
            "remote_model": "vitotrol300", "remote_slot": 1, "invert_serial": False,
        }
        self.assertEqual(self.request(
            "/api/settings", json.dumps(settings).encode(), "POST",
        )[0], 200)
        # Docker only activates its restart policy after a successful 10s start.
        time.sleep(10)
        before = int(self.docker("inspect", "-f", "{{.RestartCount}}", self.container))
        previous_instance = json.loads(self.request("/api/system")[1])["instance_id"]
        started = time.monotonic()
        self.assertEqual(self.request("/api/restart", b'{"confirm":true}', "POST")[0], 202)
        self.assertLess(time.monotonic() - started, 1)
        self.assertEqual(self.request("/api/restart", b'{"confirm":true}', "POST")[0], 409)
        self.assertEqual(self.request("/health")[0], 200)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if int(self.docker("inspect", "-f", "{{.RestartCount}}", self.container)) > before:
                break
            time.sleep(0.1)
        else:
            self.fail("Container runtime did not actually restart the container")
        self.assertGreaterEqual(time.monotonic() - started, 1)
        self.wait_for_health(30)
        self.assertEqual(json.loads(self.request("/data")[1])["protocol"], 0)
        system = json.loads(self.request("/api/system")[1])
        self.assertFalse(system["restart_pending"])
        self.assertNotEqual(system["instance_id"], previous_instance)

    def test_without_runtime_policy_exits_75_instead_of_reexec(self):
        self.docker("update", "--restart=no", self.container)
        system = json.loads(self.request("/api/system")[1])
        self.assertTrue(system["restart_manager_required"])
        self.assertEqual(self.request("/api/restart", b'{"confirm":true}', "POST")[0], 202)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if self.docker("inspect", "-f", "{{.State.Status}}", self.container) == "exited":
                break
            time.sleep(0.1)
        else:
            self.fail("Restart request did not exit the container entrypoint")
        self.assertEqual(self.docker("inspect", "-f", "{{.State.ExitCode}}", self.container), "75")
        self.assertEqual(self.docker("inspect", "-f", "{{.RestartCount}}", self.container), "0")


if __name__ == "__main__":
    unittest.main()
