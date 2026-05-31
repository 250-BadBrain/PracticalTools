package main

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

const (
	outputFile = "output.txt"
	buildDir   = ".invisible_build"
	pidFile    = "invisiblecompiler.pid"
	stopFile   = "stop"
	timeout    = 3 * time.Second
)

var sourceCandidates = []string{"input.cpp", "input.cc", "input.cxx", "input.c"}

type sourceInfo struct {
	Path     string
	Kind     string
	Compiler string
	StdFlag  string
}

func main() {
	root, err := os.Getwd()
	if err != nil {
		return
	}

	running, err := toggleExisting(root)
	if err != nil || running {
		return
	}
	defer cleanupState(root)

	source, err := ensureSource(root)
	if err != nil {
		_ = writeOutput(root, fmt.Sprintf("[Init] Failed\n\n%v\n", err))
		return
	}

	_ = compileAndRun(root, source)
	watch(root, source)
}

func watch(root string, source sourceInfo) {
	var lastMod time.Time
	var lastSize int64 = -1
	if info, err := os.Stat(source.Path); err == nil {
		lastMod = info.ModTime()
		lastSize = info.Size()
	}

	for {
		if stopRequested(root) {
			return
		}
		info, err := os.Stat(source.Path)
		if err == nil && (info.ModTime() != lastMod || info.Size() != lastSize) {
			lastMod = info.ModTime()
			lastSize = info.Size()
			time.Sleep(250 * time.Millisecond)
			_ = compileAndRun(root, source)
		}
		time.Sleep(500 * time.Millisecond)
	}
}

func toggleExisting(root string) (bool, error) {
	if err := os.MkdirAll(filepath.Join(root, buildDir), 0755); err != nil {
		return false, err
	}

	pidPath := filepath.Join(root, buildDir, pidFile)
	if data, err := os.ReadFile(pidPath); err == nil {
		pidText := strings.TrimSpace(string(data))
		if pidText != "" && processRunning(pidText) {
			return true, os.WriteFile(filepath.Join(root, buildDir, stopFile), []byte("stop"), 0644)
		}
	}

	_ = os.Remove(filepath.Join(root, buildDir, stopFile))
	return false, os.WriteFile(pidPath, []byte(fmt.Sprintf("%d\n", os.Getpid())), 0644)
}

func stopRequested(root string) bool {
	_, err := os.Stat(filepath.Join(root, buildDir, stopFile))
	return err == nil
}

func cleanupState(root string) {
	_ = os.Remove(filepath.Join(root, buildDir, stopFile))
	_ = os.Remove(filepath.Join(root, buildDir, pidFile))
}

func ensureSource(root string) (sourceInfo, error) {
	for _, name := range sourceCandidates {
		path := filepath.Join(root, name)
		if _, err := os.Stat(path); err == nil {
			return sourceFromPath(path)
		}
	}

	path := filepath.Join(root, "input.cpp")
	content := `#include <iostream>

int main() {
    std::cout << "Hello, Invisible Compiler!" << std::endl;
    return 0;
}
`
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		return sourceInfo{}, err
	}
	return sourceFromPath(path)
}

func sourceFromPath(path string) (sourceInfo, error) {
	ext := strings.ToLower(filepath.Ext(path))
	switch ext {
	case ".c":
		return sourceInfo{Path: path, Kind: "C", Compiler: "gcc", StdFlag: "-std=c11"}, nil
	case ".cpp", ".cc", ".cxx":
		return sourceInfo{Path: path, Kind: "C++", Compiler: "g++", StdFlag: "-std=c++17"}, nil
	default:
		return sourceInfo{}, fmt.Errorf("unsupported source file: %s", path)
	}
}

func compileAndRun(root string, source sourceInfo) error {
	start := time.Now()
	if _, err := exec.LookPath(source.Compiler); err != nil {
		return writeOutput(root, fmt.Sprintf("[Compiler] Not Found\n\n%s was not found in PATH.\n", source.Compiler))
	}

	if err := os.MkdirAll(filepath.Join(root, buildDir), 0755); err != nil {
		return writeOutput(root, fmt.Sprintf("[Build Directory] Failed\n\n%v\n", err))
	}

	program := filepath.Join(root, buildDir, "program")
	if runtime.GOOS == "windows" {
		program += ".exe"
	}

	compileStdout, compileStderr, err := runCommand(root, timeout, source.Compiler, source.StdFlag, "-O0", source.Path, "-o", program)
	if err != nil {
		text := strings.Builder{}
		text.WriteString("[Compile] Failed\n\n")
		text.WriteString("Source: " + filepath.Base(source.Path) + "\n")
		text.WriteString("Compiler: " + source.Compiler + "\n\n")
		appendOutput(&text, "stdout", compileStdout)
		appendOutput(&text, "stderr", compileStderr)
		text.WriteString("\nError: " + err.Error() + "\n")
		return writeOutput(root, text.String())
	}

	runStdout, runStderr, runErr := runCommand(root, timeout, program)
	elapsed := time.Since(start).Round(time.Millisecond)

	text := strings.Builder{}
	text.WriteString("[Compile] OK\n")
	text.WriteString("Source: " + filepath.Base(source.Path) + "\n")
	text.WriteString("Language: " + source.Kind + "\n\n")
	if runErr != nil {
		text.WriteString("[Run] Failed\n")
	} else {
		text.WriteString("[Run] OK\n")
	}
	text.WriteString("Time: " + elapsed.String() + "\n")
	if runErr != nil {
		text.WriteString("Error: " + runErr.Error() + "\n")
	}
	text.WriteString("\n")
	appendOutput(&text, "stdout", runStdout)
	appendOutput(&text, "stderr", runStderr)
	return writeOutput(root, text.String())
}

func runCommand(dir string, limit time.Duration, name string, args ...string) (string, string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), limit)
	defer cancel()

	cmd := exec.CommandContext(ctx, name, args...)
	cmd.Dir = dir
	prepareCommand(cmd)

	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	err := cmd.Run()
	if errors.Is(ctx.Err(), context.DeadlineExceeded) {
		err = fmt.Errorf("timeout after %s", limit)
	}
	return stdout.String(), stderr.String(), err
}

func appendOutput(builder *strings.Builder, name, value string) {
	builder.WriteString("[" + name + "]\n")
	if value == "" {
		builder.WriteString("(empty)\n\n")
		return
	}
	builder.WriteString(value)
	if !strings.HasSuffix(value, "\n") {
		builder.WriteString("\n")
	}
	builder.WriteString("\n")
}

func writeOutput(root, text string) error {
	return os.WriteFile(filepath.Join(root, outputFile), []byte(text), 0644)
}

func containsPID(text, pid string) bool {
	for _, field := range strings.Fields(text) {
		if field == pid {
			return true
		}
	}
	return false
}
