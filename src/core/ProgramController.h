// Pure state machine for the program bus.
//
// The operator *selects* content in the grid (green) and *takes* it to the
// program (red). The controller decides whether a command is allowed, keeps
// the selected/program/previous references consistent and rejects conflicting
// commands while a transition or a fade-to-black is running. The engine
// executes the accepted decision and reports completion back.
#pragma once

#include "StudioConfig.h"

#include <QString>

namespace ms {

struct ContentRef {
	enum class Type { None, Source, Scene };
	Type type = Type::None;
	QString uid;

	static ContentRef source(const QString &uid) { return {Type::Source, uid}; }
	static ContentRef scene(const QString &uid) { return {Type::Scene, uid}; }
	bool isValid() const { return type != Type::None && !uid.isEmpty(); }
	bool operator==(const ContentRef &o) const { return type == o.type && uid == o.uid; }
	bool operator!=(const ContentRef &o) const { return !(*this == o); }
	QString key() const;
	static ContentRef fromKey(const QString &key);
};

enum class TakeMode { Cut, Auto };

struct TakeDecision {
	bool accepted = false;
	QString reason;  // why it was rejected, or a warning when accepted
	ContentRef target;
	TransitionType type = TransitionType::Cut;
	int durationMs = 0;
	bool isCut() const { return type == TransitionType::Cut || durationMs <= 0; }
};

enum class FtbState { Off, FadingOut, Black, FadingIn };

class ProgramController {
public:
	void select(const ContentRef &ref);
	void clearSelection() { m_selected = {}; }

	const ContentRef &selected() const { return m_selected; }
	const ContentRef &program() const { return m_program; }
	const ContentRef &previous() const { return m_previous; }
	const ContentRef &pendingTarget() const { return m_pending; }
	bool isTransitioning() const { return m_transitioning; }
	FtbState ftbState() const { return m_ftb; }
	bool isBusy() const { return m_transitioning || m_ftb == FtbState::FadingOut || m_ftb == FtbState::FadingIn; }

	// Take the selected content (or `explicitTarget` when valid).
	TakeDecision requestTake(TakeMode mode, const TransitionSettings &tr, const ContentRef &explicitTarget = {});
	// Take back the previous program content (used by media "return to previous").
	TakeDecision requestReturnToPrevious(const TransitionSettings &tr);

	// Engine feedback.
	void transitionStarted(const ContentRef &target);
	void transitionFinished();
	void cutApplied(const ContentRef &target);
	void transitionAborted();

	TakeDecision requestFadeToBlack(int durationMs);
	void ftbFinished();

	// A source or scene was deleted. Returns true when it was on program.
	bool contentRemoved(const ContentRef &ref);

	void reset();

private:
	ContentRef m_selected;
	ContentRef m_program;
	ContentRef m_previous;
	ContentRef m_pending;
	bool m_transitioning = false;
	FtbState m_ftb = FtbState::Off;
};

} // namespace ms
