//go:build !cli && !native

package main

import (
	"flag"
	"fmt"
	"html/template"
	"net/http"
	"os/exec"
	"runtime"
	"time"
)

type guiPageData struct {
	Theme      string
	Proto      string
	State      string
	Port       string
	Keyword    string
	TestHost   string
	TestPort   string
	TestResult string
	Error      string
	Ports      []PortInfo
	Total      int
}

func runDefault() error {
	return runGUI(nil)
}

func runGUI(args []string) error {
	fs := flag.NewFlagSet("gui", flag.ExitOnError)
	addr := fs.String("addr", "127.0.0.1:8765", "GUI listen address")
	if err := fs.Parse(args); err != nil {
		return err
	}

	tmpl := template.Must(template.New("gui").Parse(guiHTML))
	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		query := r.URL.Query()
		pageTheme := query.Get("theme")
		if pageTheme == "" {
			pageTheme = "system"
		}
		data := guiPageData{
			Theme:    pageTheme,
			Proto:    query.Get("proto"),
			State:    query.Get("state"),
			Port:     query.Get("port"),
			Keyword:  query.Get("keyword"),
			TestHost: query.Get("test_host"),
			TestPort: query.Get("test_port"),
		}

		ports, err := listPorts()
		if err != nil {
			data.Error = err.Error()
		} else {
			data.Ports = filterPorts(ports, data.Proto, data.State, data.Port, data.Keyword)
			data.Total = len(data.Ports)
		}

		if data.TestHost != "" && data.TestPort != "" {
			data.TestResult = testTCPPort(data.TestHost, data.TestPort, 3*time.Second)
		}

		if err := tmpl.Execute(w, data); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
		}
	})

	url := "http://" + *addr
	fmt.Println("PortChecker GUI:", url)
	openBrowser(url)
	return http.ListenAndServe(*addr, mux)
}

func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	case "darwin":
		cmd = exec.Command("open", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	_ = cmd.Start()
}

const guiHTML = `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>PortChecker</title>
  <style>
    :root { color-scheme: light dark; --bg:#f6f7f9; --panel:#ffffff; --field:#ffffff; --head:#eef2f7; --line:#d9dee7; --text:#17202f; --muted:#617086; --accent:#2563eb; --good:#087f5b; --bad:#b42318; }
    @media (prefers-color-scheme: dark) {
      body[data-theme="system"] { --bg:#111827; --panel:#1f2937; --field:#111827; --head:#253244; --line:#374151; --text:#e5e7eb; --muted:#9ca3af; --accent:#60a5fa; --good:#34d399; --bad:#f87171; }
    }
    body[data-theme="light"] { --bg:#f6f7f9; --panel:#ffffff; --field:#ffffff; --head:#eef2f7; --line:#d9dee7; --text:#17202f; --muted:#617086; --accent:#2563eb; --good:#087f5b; --bad:#b42318; }
    body[data-theme="dark"] { --bg:#111827; --panel:#1f2937; --field:#111827; --head:#253244; --line:#374151; --text:#e5e7eb; --muted:#9ca3af; --accent:#60a5fa; --good:#34d399; --bad:#f87171; }
    * { box-sizing: border-box; }
    body { margin:0; background:var(--bg); color:var(--text); font-family:Segoe UI, Arial, sans-serif; font-size:14px; }
    header { height:56px; display:flex; align-items:center; justify-content:space-between; padding:0 20px; background:var(--panel); border-bottom:1px solid var(--line); }
    h1 { margin:0; font-size:20px; font-weight:650; }
    main { padding:18px; max-width:1280px; margin:0 auto; }
    .toolbar, .testbar { display:grid; gap:10px; align-items:end; background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:12px; }
    .toolbar { grid-template-columns: 150px 130px 160px 140px minmax(220px, 1fr) auto auto; }
    .testbar { grid-template-columns: minmax(220px, 1fr) 140px auto; margin-top:12px; }
    label { display:grid; gap:5px; color:var(--muted); font-size:12px; }
    input, select, button { height:34px; border:1px solid var(--line); border-radius:6px; padding:0 10px; font:inherit; background:var(--field); color:var(--text); }
    button { cursor:pointer; background:var(--accent); color:#fff; border-color:var(--accent); min-width:82px; }
    a.button { height:34px; display:inline-flex; align-items:center; justify-content:center; border:1px solid var(--line); border-radius:6px; padding:0 10px; color:var(--text); background:#fff; text-decoration:none; min-width:82px; }
    .summary { display:flex; align-items:center; justify-content:space-between; margin:14px 0 8px; color:var(--muted); }
    .result, .error { margin-top:12px; border-radius:8px; padding:10px 12px; background:var(--panel); border:1px solid var(--line); }
    .result { color:var(--good); }
    .error { color:var(--bad); }
    .tablewrap { overflow:auto; background:var(--panel); border:1px solid var(--line); border-radius:8px; }
    table { width:100%; border-collapse:collapse; min-width:880px; }
    th, td { padding:9px 10px; border-bottom:1px solid var(--line); text-align:left; white-space:nowrap; }
    th { position:sticky; top:0; background:var(--head); z-index:1; font-size:12px; color:var(--muted); }
    tr:last-child td { border-bottom:0; }
    code { font-family:Consolas, monospace; }
    @media (max-width: 860px) {
      header { padding:0 14px; }
      main { padding:12px; }
      .toolbar, .testbar { grid-template-columns: 1fr; }
      button, a.button { width:100%; }
    }
  </style>
</head>
<body data-theme="{{.Theme}}">
  <header>
    <h1>PortChecker</h1>
    <span>Local GUI</span>
  </header>
  <main>
    <form class="toolbar" method="get">
      <label>Theme
        <select name="theme">
          <option value="system" {{if eq .Theme "system"}}selected{{end}}>System Theme</option>
          <option value="light" {{if eq .Theme "light"}}selected{{end}}>Light Theme</option>
          <option value="dark" {{if eq .Theme "dark"}}selected{{end}}>Dark Theme</option>
        </select>
      </label>
      <label>Protocol
        <select name="proto">
          <option value="">All Protocols</option>
          <option value="tcp" {{if eq .Proto "tcp"}}selected{{end}}>TCP</option>
          <option value="udp" {{if eq .Proto "udp"}}selected{{end}}>UDP</option>
        </select>
      </label>
      <label>State
        <select name="state">
          <option value="">All States</option>
          <option value="listen" {{if eq .State "listen"}}selected{{end}}>Listen</option>
          <option value="established" {{if eq .State "established"}}selected{{end}}>Established</option>
          <option value="time_wait" {{if eq .State "time_wait"}}selected{{end}}>Time wait</option>
        </select>
      </label>
      <label>Port
        <input name="port" value="{{.Port}}" inputmode="numeric" placeholder="443">
      </label>
      <label>Keyword
        <input name="keyword" value="{{.Keyword}}" placeholder="process, pid, address">
      </label>
      <button type="submit">Filter</button>
      <a class="button" href="/">Reset</a>
    </form>

    <form class="testbar" method="get">
      <input type="hidden" name="theme" value="{{.Theme}}">
      <input type="hidden" name="proto" value="{{.Proto}}">
      <input type="hidden" name="state" value="{{.State}}">
      <input type="hidden" name="port" value="{{.Port}}">
      <input type="hidden" name="keyword" value="{{.Keyword}}">
      <label>Remote Host
        <input name="test_host" value="{{.TestHost}}" placeholder="example.com">
      </label>
      <label>Remote Port
        <input name="test_port" value="{{.TestPort}}" inputmode="numeric" placeholder="443">
      </label>
      <button type="submit">Test</button>
    </form>

    {{if .TestResult}}<div class="result">{{.TestResult}}</div>{{end}}
    {{if .Error}}<div class="error">{{.Error}}</div>{{end}}

    <div class="summary">
      <span>Total: {{.Total}}</span>
      <span>Refresh page to update ports</span>
    </div>

    <div class="tablewrap">
      <table>
        <thead>
          <tr>
            <th>PROTO</th>
            <th>STATE</th>
            <th>LOCAL</th>
            <th>REMOTE</th>
            <th>PID</th>
            <th>PROCESS</th>
          </tr>
        </thead>
        <tbody>
          {{range .Ports}}
          <tr>
            <td><code>{{.Proto}}</code></td>
            <td>{{.State}}</td>
            <td><code>{{.Local}}</code></td>
            <td><code>{{.Remote}}</code></td>
            <td><code>{{.PID}}</code></td>
            <td>{{.Process}}</td>
          </tr>
          {{end}}
        </tbody>
      </table>
    </div>
  </main>
</body>
</html>`
