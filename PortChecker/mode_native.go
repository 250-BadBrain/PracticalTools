//go:build native && (windows || linux)

package main

import (
	"strings"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
)

func runNativeGUI(args []string) error {
	a := app.New()
	w := a.NewWindow("PortChecker")
	w.Resize(fyne.NewSize(1080, 680))

	themeSelect := widget.NewSelect([]string{"System Theme", "Light Theme", "Dark Theme"}, func(value string) {
		applyNativeTheme(a, value)
	})
	themeSelect.SetSelected("System Theme")
	protoSelect := widget.NewSelect([]string{"All Protocols", "TCP", "UDP"}, nil)
	protoSelect.SetSelected("All Protocols")
	stateSelect := widget.NewSelect([]string{"All States", "Listen", "Established", "Time Wait", "UDP"}, nil)
	stateSelect.SetSelected("All States")
	portEntry := widget.NewEntry()
	portEntry.SetPlaceHolder("Port")
	keywordEntry := widget.NewEntry()
	keywordEntry.SetPlaceHolder("Keyword")
	statusLabel := widget.NewLabel("Ready")

	headers := []string{"PROTO", "STATE", "LOCAL", "REMOTE", "PID", "PROCESS"}
	rows := [][]string{}

	table := widget.NewTable(
		func() (int, int) {
			return len(rows) + 1, len(headers)
		},
		func() fyne.CanvasObject {
			label := widget.NewLabel("")
			label.Wrapping = fyne.TextTruncate
			return label
		},
		func(id widget.TableCellID, object fyne.CanvasObject) {
			label := object.(*widget.Label)
			if id.Row == 0 {
				label.TextStyle = fyne.TextStyle{Bold: true}
				label.SetText(headers[id.Col])
				return
			}
			label.TextStyle = fyne.TextStyle{}
			label.SetText(rows[id.Row-1][id.Col])
		},
	)
	table.SetColumnWidth(0, 70)
	table.SetColumnWidth(1, 110)
	table.SetColumnWidth(2, 230)
	table.SetColumnWidth(3, 230)
	table.SetColumnWidth(4, 90)
	table.SetColumnWidth(5, 220)

	refresh := func() {
		ports, err := listPorts()
		if err != nil {
			statusLabel.SetText("Error: " + err.Error())
			rows = nil
			table.Refresh()
			return
		}

		proto := selectValue(protoSelect.Selected)
		state := stateValue(stateSelect.Selected)
		filtered := filterPorts(ports, proto, state, strings.TrimSpace(portEntry.Text), strings.TrimSpace(keywordEntry.Text))
		rows = make([][]string, 0, len(filtered))
		for _, port := range filtered {
			rows = append(rows, []string{port.Proto, port.State, port.Local, port.Remote, port.PID, port.Process})
		}
		statusLabel.SetText("Total: " + intText(len(rows)))
		table.Refresh()
	}

	filterButton := widget.NewButton("Filter", refresh)
	resetButton := widget.NewButton("Reset", func() {
		protoSelect.SetSelected("All Protocols")
		stateSelect.SetSelected("All States")
		portEntry.SetText("")
		keywordEntry.SetText("")
		refresh()
	})

	hostEntry := widget.NewEntry()
	hostEntry.SetPlaceHolder("Remote host")
	remotePortEntry := widget.NewEntry()
	remotePortEntry.SetPlaceHolder("Remote port")
	testResult := widget.NewLabel("")
	testButton := widget.NewButton("Test", func() {
		host := strings.TrimSpace(hostEntry.Text)
		port := strings.TrimSpace(remotePortEntry.Text)
		if host == "" || port == "" {
			testResult.SetText("Host and port are required.")
			return
		}
		testResult.SetText(testTCPPort(host, port, 3*time.Second))
	})

	filters := container.NewGridWithColumns(7,
		themeSelect,
		protoSelect,
		stateSelect,
		portEntry,
		keywordEntry,
		filterButton,
		resetButton,
	)
	remoteTest := container.NewGridWithColumns(3, hostEntry, remotePortEntry, testButton)
	top := container.NewVBox(filters, remoteTest, testResult, statusLabel)
	w.SetContent(container.NewBorder(top, nil, nil, nil, table))

	refresh()
	w.ShowAndRun()
	return nil
}

func applyNativeTheme(a fyne.App, value string) {
	switch value {
	case "Light Theme":
		a.Settings().SetTheme(theme.LightTheme())
	case "Dark Theme":
		a.Settings().SetTheme(theme.DarkTheme())
	default:
		a.Settings().SetTheme(theme.DefaultTheme())
	}
}

func selectValue(value string) string {
	if value == "All Protocols" {
		return ""
	}
	return strings.ToLower(value)
}

func stateValue(value string) string {
	switch value {
	case "All States":
		return ""
	case "Listen":
		return "listen"
	case "Time Wait":
		return "time_wait"
	default:
		return strings.ToLower(value)
	}
}

func intText(value int) string {
	if value == 0 {
		return "0"
	}
	var digits []byte
	for value > 0 {
		digits = append(digits, byte('0'+value%10))
		value /= 10
	}
	for i, j := 0, len(digits)-1; i < j; i, j = i+1, j-1 {
		digits[i], digits[j] = digits[j], digits[i]
	}
	return string(digits)
}
