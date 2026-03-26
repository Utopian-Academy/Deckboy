import { InstanceBase, runEntrypoint, InstanceStatus } from '@companion-module/base'
import { getActions } from './actions.js'
import { getFeedbacks } from './feedbacks.js'
import { getPresets } from './presets.js'
import { VARIABLES, updateVariables } from './variables.js'
import { DeckboyConnection } from './connection.js'

class DeckboyInstance extends InstanceBase {
	async init(config) {
		this.config = config
		this.state = {}
		this.connection = null

		this.setActionDefinitions(getActions(this))
		this.setFeedbackDefinitions(getFeedbacks(this))
		this.setVariableDefinitions(VARIABLES)
		this.setPresetDefinitions(getPresets())

		if (this.config.host) {
			this.startConnection()
		} else {
			this.updateStatus(InstanceStatus.BadConfig, 'No host configured')
		}
	}

	async destroy() {
		this.stopConnection()
	}

	async configUpdated(config) {
		const hostChanged = config.host !== this.config.host || config.port !== this.config.port
		this.config = config

		if (hostChanged) {
			this.stopConnection()
			if (this.config.host) {
				this.startConnection()
			}
		}
	}

	getConfigFields() {
		return [
			{
				type: 'textinput',
				id: 'host',
				label: 'Target IP',
				width: 8,
				default: '127.0.0.1',
			},
			{
				type: 'number',
				id: 'port',
				label: 'TCP Port',
				width: 4,
				default: 5510,
				min: 1,
				max: 65535,
			},
			{
				type: 'number',
				id: 'pollInterval',
				label: 'Status poll interval (ms)',
				width: 4,
				default: 250,
				min: 50,
				max: 5000,
			},
		]
	}

	startConnection() {
		this.connection = new DeckboyConnection(
			this.config.host,
			this.config.port || 5510,
			this.config.pollInterval || 250,
			{
				onConnected: () => {
					this.updateStatus(InstanceStatus.Ok)
					this.log('info', `Connected to Deckboy at ${this.config.host}:${this.config.port || 5510}`)
				},
				onDisconnected: () => {
					this.updateStatus(InstanceStatus.Disconnected)
				},
				onError: (msg) => {
					this.updateStatus(InstanceStatus.ConnectionFailure, msg)
				},
				onState: (state) => {
					this.state = state
					updateVariables(this, state)
					this.checkFeedbacks()
				},
			}
		)
	}

	stopConnection() {
		if (this.connection) {
			this.connection.destroy()
			this.connection = null
		}
	}

	sendCommand(cmd) {
		if (this.connection) {
			this.connection.send(cmd)
		} else {
			this.log('warn', `Cannot send "${cmd}" — not connected`)
		}
	}
}

runEntrypoint(DeckboyInstance, [])
