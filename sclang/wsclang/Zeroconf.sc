ZeroconfTarget
{
	var <name;
	var <domain;
	var <address;
	var <port;
	var <>onDisconnected;

	*new { |name, domain, address, port|
		^this.newCopyArgs(name, domain, address, port, nil)
	}
}

ZeroconfBrowser
{
	var m_ptr;
	var m_type;
	var <>onTargetResolved;
	var m_targets; // note: those are acquired targets..

	classvar g_instances;

	*initClass {
		g_instances = [];
		ShutDown.add({
			g_instances.do(_.free());
		})
	}

	*new { |type, target = "", onTargetResolved|
		^this.newCopyArgs(0x0, type, onTargetResolved).zCtor(target)
	}

	*newMonitor { |type|
	}

	zCtor { |target|
		m_targets = [];
		g_instances = g_instances.add(this);
		this.prmCreate(m_type);
		if (target.notEmpty()) {
			this.addTarget(target);
		}
	}

	prmCreate { |type|
		_ZeroconfBrowserCreate
		^this.primitiveFailed
	}

	addTarget { |target|
		_ZeroconfBrowserAddTarget
		^this.primitiveFailed
	}

	removeTarget { |target|
		_ZeroconfBrowserRemoveTarget
		^this.primitiveFailed
	}

	at { |index|
		^m_targets[index];
	}

	pvOnTargetResolved { |name, domain, address, port|
		var target = ZeroconfTarget(name, domain, address, port.asInteger);
		if (onTargetResolved.notNil()) {
			onTargetResolved.value(target);
		};
		m_targets = m_targets.add(target);
	}

	pvOnTargetRemoved { |name|
		var rem;
		m_targets.do({ |target|
			if (target.name == name) {
				target.onDisconnected.value();
				rem = target;
			}
		});
		if (rem.notNil()) {
			m_targets.removeAll(rem);
		}
	}

	free {
		g_instances.remove(this);
		this.prmFree();
	}

	prmFree {
		_ZeroconfBrowserFree
		^this.primitiveFailed
	}

}

ZeroconfService
{
	var m_ptr;
	var m_name;
	var m_type;
	var m_port;

	classvar g_instances;

	*initClass {
		g_instances = [];
		ShutDown.add({
			g_instances.do(_.free());
		})
	}

	*new { |name, type, port|
		^this.newCopyArgs(0x0, name, type, port).zCtor()
	}

	zCtor {
		g_instances = g_instances.add(this);
		this.prmAddService(m_name, m_type, m_port);
	}

	prmAddService { |name, type, port|
		_ZeroconfAddService
		^this.primitiveFailed
	}

	free {
		g_instances.remove(this);
		this.prmFree();
	}

	prmFree {
		_ZeroconfRemoveService
		^this.primitiveFailed
	}
}