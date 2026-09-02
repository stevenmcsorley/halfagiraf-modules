#pragma once
// Three independently-stored values edited through one physical knob.
// Bad Sector uses a virtual Rack knob, so a selector press can move the knob
// pointer to the recalled value and edit it immediately.
struct BsSelector {
	float vals[3] = {0.f, 0.f, 0.f};
	int sel = 0;
	bool caught = true;
	float lastKnob = 0.f;

	void reset(float a, float b, float c) {
		vals[0] = a; vals[1] = b; vals[2] = c;
		sel = 0;
		caught = true;
		lastKnob = vals[0];
	}

	// Store the channel being left, select the next one, and return the value
	// that Rack's virtual knob should display.
	float advanceSnap(float knob) {
		vals[sel] = knob;
		sel = (sel + 1) % 3;
		caught = true;
		lastKnob = vals[sel];
		return lastKnob;
	}

	// Return the selected stored value after a patch or preset load.
	float selectedSnap() {
		caught = true;
		lastKnob = vals[sel];
		return lastKnob;
	}

	// Call every sample with the virtual knob position.
	float track(float knob) {
		caught = true;
		vals[sel] = knob;
		lastKnob = knob;
		return vals[sel];
	}
};
