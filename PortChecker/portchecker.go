package main

import (
	"bufio"
	"encoding/csv"
	"errors"
	"flag"
	"fmt"
	"net"
	"os"
	"os/exec"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"time"
)

type PortInfo struct {
	Proto   string
	Local   string
	Port    string
	Remote  string
	State   string
	PID     string
	Process string
}

func main() {
	if len(os.Args) < 2 {
		if err := runDefault(); err != nil {
			fmt.Fprintln(os.Stderr, "Error:", err)
			os.Exit(1)
		}
		return
	}

	var err error
	switch os.Args[1] {
	case "list":
		err = runList(os.Args[2:])
	case "find":
		err = runFind(os.Args[2:])
	case "test":
		err = runTest(os.Args[2:])
	case "gui":
		err = runGUI(os.Args[2:])
	case "menu":
		runInteractive()
	case "help", "-h", "--help":
		printHelp()
	default:
		err = fmt.Errorf("unknown command: %s", os.Args[1])
	}

	if err != nil {
		fmt.Fprintln(os.Stderr, "Error:", err)
		os.Exit(1)
	}
}

func runInteractive() {
	reader := bufio.NewReader(os.Stdin)
	for {
		fmt.Println()
		fmt.Println("PortChecker")
		fmt.Println("1. List ports")
		fmt.Println("2. Filter ports")
		fmt.Println("3. Find by keyword")
		fmt.Println("4. Test remote TCP port")
		fmt.Println("5. Help")
		fmt.Println("6. Exit")
		fmt.Println()
		choice := prompt(reader, "Choose [1-6]: ")

		var err error
		switch choice {
		case "1":
			err = runList(nil)
		case "2":
			err = interactiveFilter(reader)
		case "3":
			keyword := prompt(reader, "Keyword: ")
			if keyword != "" {
				err = runFind([]string{keyword})
			}
		case "4":
			host := prompt(reader, "Host or IP: ")
			port := prompt(reader, "Port: ")
			if host != "" && port != "" {
				err = runTest([]string{host, port})
			}
		case "5":
			printHelp()
		case "6", "q", "Q":
			return
		default:
			fmt.Println("Unknown choice.")
		}

		if err != nil {
			fmt.Fprintln(os.Stderr, "Error:", err)
		}
		prompt(reader, "Press Enter to continue...")
	}
}

func interactiveFilter(reader *bufio.Reader) error {
	proto := prompt(reader, "Protocol, tcp/udp or empty: ")
	state := prompt(reader, "State, listen/established or empty: ")
	port := prompt(reader, "Local port or empty: ")
	keyword := prompt(reader, "Keyword or empty: ")
	args := []string{}
	if proto != "" {
		args = append(args, "--proto", proto)
	}
	if state != "" {
		args = append(args, "--state", state)
	}
	if port != "" {
		args = append(args, "--port", port)
	}
	if keyword != "" {
		args = append(args, "--filter", keyword)
	}
	return runList(args)
}

func prompt(reader *bufio.Reader, label string) string {
	fmt.Print(label)
	text, _ := reader.ReadString('\n')
	return strings.TrimSpace(text)
}

func testTCPPort(host, port string, timeout time.Duration) string {
	address := net.JoinHostPort(host, port)
	start := time.Now()
	conn, err := net.DialTimeout("tcp", address, timeout)
	elapsed := time.Since(start).Round(time.Millisecond)
	if err != nil {
		return fmt.Sprintf("Host: %s | Port: %s | Open: no | Time: %s | Reason: %v", host, port, elapsed, err)
	}
	_ = conn.Close()
	return fmt.Sprintf("Host: %s | Port: %s | Open: yes | Time: %s", host, port, elapsed)
}

func runList(args []string) error {
	fs := flag.NewFlagSet("list", flag.ExitOnError)
	proto := fs.String("proto", "", "tcp or udp")
	state := fs.String("state", "", "listening, established, or other state")
	port := fs.String("port", "", "exact local port")
	filter := fs.String("filter", "", "keyword in address, port, pid, process, or state")
	if err := fs.Parse(args); err != nil {
		return err
	}

	ports, err := listPorts()
	if err != nil {
		return err
	}
	ports = filterPorts(ports, *proto, *state, *port, *filter)
	printPorts(ports)
	return nil
}

func runFind(args []string) error {
	if len(args) != 1 {
		return errors.New("usage: portchecker find KEYWORD")
	}
	ports, err := listPorts()
	if err != nil {
		return err
	}
	printPorts(filterPorts(ports, "", "", "", args[0]))
	return nil
}

func runTest(args []string) error {
	fs := flag.NewFlagSet("test", flag.ExitOnError)
	timeout := fs.Duration("timeout", 3*time.Second, "connection timeout")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if fs.NArg() != 2 {
		return errors.New("usage: portchecker test HOST PORT")
	}

	host := fs.Arg(0)
	port := fs.Arg(1)
	fmt.Println(strings.ReplaceAll(testTCPPort(host, port, *timeout), " | ", "\n"))
	return nil
}

func listPorts() ([]PortInfo, error) {
	switch runtime.GOOS {
	case "windows":
		return listWindowsPorts()
	case "linux":
		return listLinuxPorts()
	default:
		return nil, fmt.Errorf("%s is not supported yet; remote test still works", runtime.GOOS)
	}
}

func listWindowsPorts() ([]PortInfo, error) {
	out, err := commandOutput("netstat", "-ano")
	if err != nil {
		return nil, err
	}
	processes := windowsProcesses()
	var ports []PortInfo
	for _, line := range strings.Split(string(out), "\n") {
		fields := strings.Fields(line)
		if len(fields) < 4 {
			continue
		}
		proto := strings.ToUpper(fields[0])
		if proto != "TCP" && proto != "UDP" {
			continue
		}
		info := PortInfo{Proto: proto, Local: fields[1]}
		info.Port = extractPort(info.Local)
		if proto == "TCP" && len(fields) >= 5 {
			info.Remote = fields[2]
			info.State = strings.ToUpper(fields[3])
			info.PID = fields[4]
		}
		if proto == "UDP" {
			info.Remote = fields[2]
			info.State = "UDP"
			info.PID = fields[3]
		}
		info.Process = processes[info.PID]
		ports = append(ports, info)
	}
	return uniqueAndSortPorts(ports), nil
}

func windowsProcesses() map[string]string {
	result := map[string]string{}
	out, err := commandOutput("tasklist", "/fo", "csv", "/nh")
	if err != nil {
		return result
	}
	reader := csv.NewReader(strings.NewReader(string(out)))
	records, err := reader.ReadAll()
	if err != nil {
		return result
	}
	for _, record := range records {
		if len(record) >= 2 {
			result[record[1]] = record[0]
		}
	}
	return result
}

func listLinuxPorts() ([]PortInfo, error) {
	if _, err := exec.LookPath("ss"); err == nil {
		return listLinuxSSPorts()
	}
	if _, err := exec.LookPath("netstat"); err == nil {
		return listLinuxNetstatPorts()
	}
	return nil, errors.New("neither ss nor netstat was found")
}

func listLinuxSSPorts() ([]PortInfo, error) {
	out, err := commandCombinedOutput("ss", "-tunap", "-H")
	if err != nil && len(out) == 0 {
		return nil, err
	}
	var ports []PortInfo
	for _, line := range strings.Split(string(out), "\n") {
		fields := strings.Fields(line)
		if len(fields) < 5 {
			continue
		}
		localIndex := 4
		remoteIndex := 5
		if len(fields) == 5 {
			localIndex = 3
			remoteIndex = 4
		}
		procText := ""
		if len(fields) > remoteIndex+1 {
			procText = strings.Join(fields[remoteIndex+1:], " ")
		}
		process, pid := parseLinuxProcess(procText)
		info := PortInfo{
			Proto:   strings.ToUpper(fields[0]),
			State:   strings.ToUpper(fields[1]),
			Local:   fields[localIndex],
			Remote:  fields[remoteIndex],
			Process: process,
			PID:     pid,
		}
		info.Port = extractPort(info.Local)
		ports = append(ports, info)
	}
	return uniqueAndSortPorts(ports), nil
}

func listLinuxNetstatPorts() ([]PortInfo, error) {
	out, err := commandCombinedOutput("netstat", "-tunlp")
	if err != nil && len(out) == 0 {
		return nil, err
	}
	var ports []PortInfo
	for _, line := range strings.Split(string(out), "\n") {
		fields := strings.Fields(line)
		if len(fields) < 6 {
			continue
		}
		proto := strings.ToUpper(fields[0])
		if !strings.HasPrefix(proto, "TCP") && !strings.HasPrefix(proto, "UDP") {
			continue
		}
		pid, process := splitPIDProgram(fields[len(fields)-1])
		info := PortInfo{
			Proto:   strings.TrimRight(proto, "46"),
			Local:   fields[3],
			Remote:  fields[4],
			State:   strings.ToUpper(fields[5]),
			PID:     pid,
			Process: process,
		}
		if strings.HasPrefix(proto, "UDP") {
			info.State = "UDP"
		}
		info.Port = extractPort(info.Local)
		ports = append(ports, info)
	}
	return uniqueAndSortPorts(ports), nil
}

func parseLinuxProcess(text string) (string, string) {
	name := ""
	pid := ""
	if idx := strings.Index(text, "(("); idx >= 0 {
		part := text[idx+2:]
		part = strings.TrimPrefix(part, "\"")
		if end := strings.Index(part, "\""); end >= 0 {
			name = part[:end]
		}
	}
	if idx := strings.Index(text, "pid="); idx >= 0 {
		part := text[idx+4:]
		for i, r := range part {
			if r < '0' || r > '9' {
				pid = part[:i]
				break
			}
		}
		if pid == "" {
			pid = part
		}
	}
	return name, pid
}

func splitPIDProgram(text string) (string, string) {
	if text == "-" {
		return "", ""
	}
	parts := strings.SplitN(text, "/", 2)
	if len(parts) != 2 {
		return "", text
	}
	return parts[0], parts[1]
}

func extractPort(address string) string {
	address = strings.Trim(address, "[]")
	if strings.HasSuffix(address, ":*") {
		return "*"
	}
	idx := strings.LastIndex(address, ":")
	if idx < 0 || idx == len(address)-1 {
		return ""
	}
	return strings.Trim(address[idx+1:], "[]")
}

func filterPorts(ports []PortInfo, proto, state, exactPort, keyword string) []PortInfo {
	proto = strings.ToUpper(strings.TrimSpace(proto))
	state = strings.ToUpper(strings.TrimSpace(state))
	exactPort = strings.TrimSpace(exactPort)
	keyword = strings.ToLower(strings.TrimSpace(keyword))
	var result []PortInfo
	for _, port := range ports {
		if proto != "" && strings.ToUpper(port.Proto) != proto {
			continue
		}
		if state != "" && !stateMatches(port.State, state) {
			continue
		}
		if exactPort != "" && port.Port != exactPort {
			continue
		}
		if keyword != "" && !strings.Contains(strings.ToLower(strings.Join([]string{
			port.Proto, port.Local, port.Port, port.Remote, port.State, port.PID, port.Process,
		}, " ")), keyword) {
			continue
		}
		result = append(result, port)
	}
	return result
}

func stateMatches(actual, expected string) bool {
	actual = strings.ToUpper(actual)
	expected = strings.ToUpper(expected)
	if expected == "LISTEN" || expected == "LISTENING" {
		return actual == "LISTEN" || actual == "LISTENING"
	}
	return actual == expected
}

func uniqueAndSortPorts(ports []PortInfo) []PortInfo {
	seen := map[string]bool{}
	var unique []PortInfo
	for _, port := range ports {
		key := strings.Join([]string{port.Proto, port.State, port.Local, port.Remote, port.PID, port.Process}, "\x00")
		if seen[key] {
			continue
		}
		seen[key] = true
		unique = append(unique, port)
	}
	sort.SliceStable(unique, func(i, j int) bool {
		left, _ := strconv.Atoi(unique[i].Port)
		right, _ := strconv.Atoi(unique[j].Port)
		if left == right {
			return unique[i].Proto < unique[j].Proto
		}
		return left < right
	})
	return unique
}

func printPorts(ports []PortInfo) {
	fmt.Printf("%-5s %-9s %-23s %-23s %-7s %-24s\n", "PROTO", "STATE", "LOCAL", "REMOTE", "PID", "PROCESS")
	fmt.Println(strings.Repeat("-", 96))
	for _, port := range ports {
		fmt.Printf("%-5s %-9s %-23s %-23s %-7s %-24s\n",
			port.Proto, port.State, port.Local, port.Remote, port.PID, port.Process)
	}
	fmt.Printf("\nTotal: %d\n", len(ports))
}

func printHelp() {
	fmt.Println(`PortChecker

Usage:
  portchecker gui [--addr 127.0.0.1:8765]
  portchecker menu
  portchecker list [--proto tcp|udp] [--state STATE] [--port PORT] [--filter KEYWORD]
  portchecker find KEYWORD
  portchecker test HOST PORT [--timeout 3s]
  portchecker help

Examples:
  portchecker gui
  portchecker list
  portchecker list --proto tcp --state listen
  portchecker list --port 443
  portchecker list --filter 443
  portchecker find nginx
  portchecker test example.com 443`)
}
