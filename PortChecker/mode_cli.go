//go:build cli

package main

import "errors"

func runDefault() error {
	runInteractive()
	return nil
}

func runGUI(args []string) error {
	return errors.New("browser GUI is not included in this CLI build")
}
