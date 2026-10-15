#include "ProgramController.h"

namespace ms {

QString ContentRef::key() const
{
	switch (type) {
	case Type::Source: return QStringLiteral("source:") + uid;
	case Type::Scene: return QStringLiteral("scene:") + uid;
	case Type::None: break;
	}
	return {};
}

ContentRef ContentRef::fromKey(const QString &key)
{
	if (key.startsWith(QLatin1String("source:")))
		return source(key.mid(7));
	if (key.startsWith(QLatin1String("scene:")))
		return scene(key.mid(6));
	return {};
}

void ProgramController::select(const ContentRef &ref)
{
	m_selected = ref;
}

TakeDecision ProgramController::requestTake(TakeMode mode, const TransitionSettings &tr,
					    const ContentRef &explicitTarget)
{
	TakeDecision d;
	d.target = explicitTarget.isValid() ? explicitTarget : m_selected;

	if (m_transitioning) {
		d.reason = QStringLiteral("Transição em andamento");
		return d;
	}
	if (m_ftb == FtbState::FadingOut || m_ftb == FtbState::FadingIn) {
		d.reason = QStringLiteral("Fade para preto em andamento");
		return d;
	}
	if (!d.target.isValid()) {
		d.reason = QStringLiteral("Nenhuma fonte selecionada");
		return d;
	}
	if (d.target == m_program) {
		d.reason = QStringLiteral("Já está no ar");
		return d;
	}

	d.accepted = true;
	if (mode == TakeMode::Cut || tr.type == TransitionType::Cut) {
		d.type = TransitionType::Cut;
		d.durationMs = 0;
	} else {
		d.type = tr.type == TransitionType::FadeToBlack ? TransitionType::Fade : tr.type;
		d.durationMs = tr.durationMs;
	}
	if (m_ftb == FtbState::Black)
		d.reason = QStringLiteral("Programa em preto (FTB ativo): a troca acontece por baixo do preto");
	return d;
}

TakeDecision ProgramController::requestReturnToPrevious(const TransitionSettings &tr)
{
	if (!m_previous.isValid()) {
		TakeDecision d;
		d.reason = QStringLiteral("Não há conteúdo anterior");
		return d;
	}
	return requestTake(TakeMode::Auto, tr, m_previous);
}

void ProgramController::transitionStarted(const ContentRef &target)
{
	m_transitioning = true;
	m_pending = target;
}

void ProgramController::transitionFinished()
{
	if (!m_transitioning)
		return;
	m_transitioning = false;
	if (m_pending != m_program) {
		m_previous = m_program;
		m_program = m_pending;
	}
	m_pending = {};
}

void ProgramController::cutApplied(const ContentRef &target)
{
	m_transitioning = false;
	m_pending = {};
	if (target != m_program) {
		m_previous = m_program;
		m_program = target;
	}
}

void ProgramController::transitionAborted()
{
	m_transitioning = false;
	m_pending = {};
}

TakeDecision ProgramController::requestFadeToBlack(int durationMs)
{
	TakeDecision d;
	if (m_transitioning) {
		d.reason = QStringLiteral("Transição em andamento");
		return d;
	}
	if (m_ftb == FtbState::FadingOut || m_ftb == FtbState::FadingIn) {
		d.reason = QStringLiteral("Fade para preto em andamento");
		return d;
	}
	d.accepted = true;
	d.type = TransitionType::FadeToBlack;
	d.durationMs = durationMs;
	m_ftb = (m_ftb == FtbState::Off) ? FtbState::FadingOut : FtbState::FadingIn;
	return d;
}

void ProgramController::ftbFinished()
{
	if (m_ftb == FtbState::FadingOut)
		m_ftb = FtbState::Black;
	else if (m_ftb == FtbState::FadingIn)
		m_ftb = FtbState::Off;
}

bool ProgramController::contentRemoved(const ContentRef &ref)
{
	if (m_selected == ref)
		m_selected = {};
	if (m_previous == ref)
		m_previous = {};
	if (m_pending == ref) {
		m_pending = {};
		m_transitioning = false;
	}
	if (m_program == ref) {
		m_program = {};
		return true;
	}
	return false;
}

void ProgramController::reset()
{
	*this = ProgramController{};
}

} // namespace ms
