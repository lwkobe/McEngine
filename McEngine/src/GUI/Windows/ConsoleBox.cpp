//================ Copyright (c) 2011, PG, All rights reserved. =================//
//
// Purpose:		textbox + scrollview command suggestion list
//
// $NoKeywords: $
//===============================================================================//

#include "ConsoleBox.h"
#include "Engine.h"
#include "Environment.h"
#include "ResourceManager.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "ConVar.h"

#include "Console.h"

#include "CBaseUITextbox.h"
#include "CBaseUIBoxShadow.h"
#include "CBaseUIScrollView.h"
#include "CBaseUIContainer.h"
#include "CBaseUIButton.h"
#include "AnimationHandler.h"

#ifdef MCENGINE_FEATURE_MULTITHREADING

#include <mutex>
#include "WinMinGW.Mutex.h"
#include "Horizon.Mutex.h"

#endif

ConVar console_animspeed("console_animspeed", 6.0f);
ConVar console_animspeed2("console_animspeed2", 12.0f);

ConVar console_overlay("console_overlay", true, "should the log overlay always be visible (or only if the console is out)");
ConVar console_overlay_lines("console_overlay_lines", 6, "max number of lines of text");
ConVar console_overlay_scale("console_overlay_scale", 1.0f, "log text size multiplier");

#ifdef MCENGINE_FEATURE_MULTITHREADING

std::mutex g_consoleBoxLogOverlayMutex;

#endif

ConsoleBox::ConsoleBox() : CBaseUIElement(0, 0, 0, 0, "")
{
	const float dpiScale = env->getDPIScale();

	m_textbox = new CBaseUITextbox(5 * dpiScale, engine->getScreenHeight(), engine->getScreenWidth() - 10 * dpiScale, 26, "consoleboxtextbox");

	McFont *font = engine->getResourceManager()->getFont("FONT_DEFAULT");
	m_logFont = engine->getResourceManager()->getFont("FONT_CONSOLE");
	m_textbox->setSizeY(m_textbox->getRelSize().y * dpiScale);
	m_textbox->setFont(font);
	m_textbox->setDrawBackground(true);
	m_textbox->setVisible(false);
	m_textbox->setBusy(true);

	m_bRequireShiftToActivate = false;
	m_fConsoleAnimation = 0;
	m_bConsoleAnimateIn = false;
	m_bConsoleAnimateOut = false;
	m_fConsoleDelay = engine->getTime() + 0.2f;
	m_bConsoleAnimateOnce = false; // set to true for on-launch anim in

	m_suggestion = new CBaseUIScrollView(5 * dpiScale, engine->getScreenHeight(), engine->getScreenWidth() - 10 * dpiScale, 90 * dpiScale, "consoleboxsuggestion");
	m_suggestion->setDrawBackground(true);
	m_suggestion->setBackgroundColor(COLOR(255, 0, 0, 0));
	m_suggestion->setFrameColor(COLOR(255, 255, 255, 255));
	m_suggestion->setHorizontalScrolling(false);
	m_suggestion->setVerticalScrolling(true);
	m_suggestion->setVisible(false);
	m_fSuggestionAnimation = 0;
	m_bSuggestionAnimateIn = false;
	m_bSuggestionAnimateOut = false;

	m_iSuggestionCount = 0;
	m_iSelectedSuggestion = -1;

	m_iSelectedHistory = -1;

	m_fLogYPos = 0.0f;

	clearSuggestions();
}

ConsoleBox::~ConsoleBox()
{
	SAFE_DELETE(m_textbox);
	SAFE_DELETE(m_suggestion);
}

void ConsoleBox::draw(Graphics *g)
{
	g->setAntialiasing(false);

	g->pushTransform();
	{
		if (engine->getMouse()->isMiddleDown())
			g->translate(0, engine->getMouse()->getPos().y - engine->getScreenHeight());

		// draw overlay
		if (console_overlay.getBool() || m_textbox->isVisible())
		{
#ifdef MCENGINE_FEATURE_MULTITHREADING

			std::lock_guard<std::mutex> lk(g_consoleBoxLogOverlayMutex);

#endif

			const float dpiScale = getDPIScale();

			const float logScale = std::round(dpiScale + 0.255f) * console_overlay_scale.getFloat();

			const int shadowOffset = 1 * logScale;

			g->setColor(0xff000000);
			if (m_fLogYPos != 0.0f)
				g->setAlpha(1.0f - (m_fLogYPos / (m_logFont->getHeight()*(console_overlay_lines.getInt()+1))));

			g->pushTransform();
			{
				g->scale(logScale, logScale);
				g->translate(2 * logScale + shadowOffset, -m_fLogYPos + shadowOffset);
				for (int i=0; i<m_log.size(); i++)
				{
					g->translate(0, (int)((m_logFont->getHeight() + (i == 0 ? 0 : 2) + 1) * logScale));
					g->drawString(m_logFont, m_log[i]);
				}
			}
			g->popTransform();

			g->setColor(0xffffffff);
			if (m_fLogYPos != 0.0f)
				g->setAlpha(1.0f - (m_fLogYPos / (m_logFont->getHeight()*(console_overlay_lines.getInt()+1))));

			g->pushTransform();
			{
				g->scale(logScale, logScale);
				g->translate(2 * logScale, -m_fLogYPos);
				for (int i=0; i<m_log.size(); i++)
				{
					g->translate(0, (int)((m_logFont->getHeight() + (i == 0 ? 0 : 2) + 1) * logScale));
					g->drawString(m_logFont, m_log[i]);
				}
			}
			g->popTransform();
		}

		// draw textbox + suggestions
		if (anim->isAnimating(&m_fConsoleAnimation))
		{
			g->push3DScene(McRect(m_textbox->getPos().x, m_textbox->getPos().y, m_textbox->getSize().x, m_textbox->getSize().y));
			{
				g->rotate3DScene(((m_fConsoleAnimation/getAnimTargetY())*130 - 130), 0, 0);
				g->translate3DScene(0, 0, ((m_fConsoleAnimation/getAnimTargetY())*500 - 500));
				m_textbox->draw(g);
				m_suggestion->draw(g);
			}
			g->pop3DScene();
		}
		else
		{
			m_suggestion->draw(g);
			m_textbox->draw(g);
		}
	}
	g->popTransform();
}

void ConsoleBox::update()
{
	CBaseUIElement::update();

	bool mleft = engine->getMouse()->isLeftDown();

	if (mleft && m_textbox->isMouseInside() && m_textbox->isVisible())
		m_textbox->setActive(true);

	// handle consolebox
	m_textbox->update();

	if (m_textbox->hitEnter())
	{
		processCommand(m_textbox->getText());
		m_textbox->clear();
	}

	if (m_bConsoleAnimateOnce)
	{
		if (engine->getTime() > m_fConsoleDelay)
		{
			m_bConsoleAnimateIn = true;
			m_bConsoleAnimateOnce = false;
			m_textbox->setVisible(true);
		}
	}

	if (m_bConsoleAnimateIn)
	{
		if (m_fConsoleAnimation < getAnimTargetY() && std::round((m_fConsoleAnimation/getAnimTargetY())*500) < 500.0f)
			m_textbox->setPosY( engine->getScreenHeight() - m_fConsoleAnimation );
		else
		{
			m_bConsoleAnimateIn = false;
			m_fConsoleAnimation = getAnimTargetY();
			m_textbox->setPosY( engine->getScreenHeight() - m_fConsoleAnimation );
			m_textbox->setActive(true);
			anim->deleteExistingAnimation(&m_fConsoleAnimation);
		}
	}

	if (m_bConsoleAnimateOut)
	{
		if (m_fConsoleAnimation > 0.0f && std::round((m_fConsoleAnimation/getAnimTargetY())*500) > 0.0f)
			m_textbox->setPosY( engine->getScreenHeight() - m_fConsoleAnimation );
		else
		{
			m_bConsoleAnimateOut = false;
			m_textbox->setVisible(false);
			m_fConsoleAnimation = 0.0f;
			m_textbox->setPosY( engine->getScreenHeight() );
			anim->deleteExistingAnimation(&m_fConsoleAnimation);
		}
	}

	// handle suggestions
	m_suggestion->update();

	if (m_bSuggestionAnimateOut)
	{
		if (m_fSuggestionAnimation <= m_fSuggestionY)
		{
			m_suggestion->setPosY( engine->getScreenHeight() - (m_fSuggestionY - m_fSuggestionAnimation) );
			m_fSuggestionAnimation += console_animspeed2.getFloat();
		}
		else
		{
			m_bSuggestionAnimateOut = false;
			m_fSuggestionAnimation = m_fSuggestionY;
			m_suggestion->setVisible(false);
			m_suggestion->setPosY( engine->getScreenHeight() );
		}
	}

	if (m_bSuggestionAnimateIn)
	{
		if (m_fSuggestionAnimation >= 0)
		{
			m_suggestion->setPosY( engine->getScreenHeight() - (m_fSuggestionY - m_fSuggestionAnimation) );
			m_fSuggestionAnimation -= console_animspeed2.getFloat();
		}
		else
		{
			m_bSuggestionAnimateIn = false;
			m_fSuggestionAnimation = 0.0f;
			m_suggestion->setPosY( engine->getScreenHeight() - m_fSuggestionY );
		}
	}

	if (mleft && !m_suggestion->isMouseInside() && !m_textbox->isActive() && !m_suggestion->isBusy())
		m_suggestion->setVisible(false);

	if (m_textbox->isActive() && mleft && m_textbox->isMouseInside() && m_iSuggestionCount > 0)
		m_suggestion->setVisible(true);

	// handle overlay animation and timeout
	if (engine->getTime() > m_fLogTime)
	{
		if (!anim->isAnimating(&m_fLogYPos) && m_fLogYPos == 0.0f)
			anim->moveQuadInOut(&m_fLogYPos, m_logFont->getHeight()*(console_overlay_lines.getFloat()+1), 0.5f);

		if (m_fLogYPos == m_logFont->getHeight()*(console_overlay_lines.getInt()+1))
			m_log.clear();
	}
}

void ConsoleBox::onSuggestionClicked(CBaseUIButton *suggestion)
{
	UString text = suggestion->getName();

	ConVar *temp = convar->getConVarByName(text, false);
	if (temp != NULL && (temp->hasValue() || temp->hasCallbackArgs()))
		text.append(" ");

	m_textbox->setText(text);
	m_textbox->setCursorPosRight();

	clearSuggestions();

	m_textbox->setActive(true);
}

void ConsoleBox::onKeyDown(KeyboardEvent &e)
{
	// toggle visibility
	if ((e == KEY_F1 && (m_textbox->isActive() && m_textbox->isVisible() && !m_bConsoleAnimateOut ? true : engine->getKeyboard()->isShiftDown())) || (m_textbox->isActive() && m_textbox->isVisible() && !m_bConsoleAnimateOut && e == KEY_ESCAPE))
	{
		if (m_textbox->isVisible() && !m_bConsoleAnimateIn  && !m_bSuggestionAnimateIn)
		{
			m_bConsoleAnimateOut = true;
			anim->moveSmoothEnd(&m_fConsoleAnimation, 0, 2.0f*0.8f);

			if (m_suggestion->getContainer()->getElements().size() > 0)
				m_bSuggestionAnimateOut = true;

			e.consume();
		}
		else if (!m_bConsoleAnimateOut && !m_bSuggestionAnimateOut)
		{
			m_textbox->setVisible(true);
			m_textbox->setActive(true);
			m_textbox->setBusy(true);
			m_bConsoleAnimateIn = true;

			anim->moveSmoothEnd(&m_fConsoleAnimation, getAnimTargetY(), 1.5f*0.6f);

			if (m_suggestion->getContainer()->getElements().size() > 0)
			{
				m_bSuggestionAnimateIn = true;
				m_suggestion->setVisible(true);
			}

			e.consume();
		}

		// force layout update
		onResolutionChange(engine->getScreenSize());
	}

	if (m_bConsoleAnimateOut) return;

	// textbox
	m_textbox->onKeyDown(e);

	// handle suggestion up/down buttons
	if (m_iSuggestionCount > 0 && m_textbox->isActive() && m_textbox->isVisible())
	{
		if (e == KEY_DOWN)
		{
			if (m_iSelectedSuggestion < 1)
				m_iSelectedSuggestion = m_iSuggestionCount - 1;
			else
				m_iSelectedSuggestion--;

			if (m_iSelectedSuggestion > -1 && m_iSelectedSuggestion < m_vSuggestionButtons.size())
			{
				UString text = m_vSuggestionButtons[m_iSelectedSuggestion]->getName();

				ConVar *temp = convar->getConVarByName(text, false);
				if (temp != NULL && (temp->hasValue() || temp->hasCallbackArgs()))
					text.append(" ");

				m_textbox->setText(text);
				m_textbox->setCursorPosRight();
				m_suggestion->scrollToElement(m_vSuggestionButtons[m_iSelectedSuggestion]);

				for (int i=0; i<m_vSuggestionButtons.size(); i++)
				{
					if (i == m_iSelectedSuggestion)
					{
						m_vSuggestionButtons[i]->setTextColor(0xff00ff00);
						m_vSuggestionButtons[i]->setTextDarkColor(0xff000000);
					}
					else
						m_vSuggestionButtons[i]->setTextColor(0xffffffff);
				}
			}

			e.consume();
		}
		else if (e == KEY_UP)
		{
			if (m_iSelectedSuggestion > m_iSuggestionCount-2)
				m_iSelectedSuggestion = 0;
			else
				m_iSelectedSuggestion++;

			if (m_iSelectedSuggestion > -1 && m_iSelectedSuggestion < m_vSuggestionButtons.size())
			{
				UString text = m_vSuggestionButtons[m_iSelectedSuggestion]->getName();

				ConVar *temp = convar->getConVarByName(text, false);
				if (temp != NULL && (temp->hasValue() || temp->hasCallbackArgs()))
					text.append(" ");

				m_textbox->setText(text);
				m_textbox->setCursorPosRight();
				m_suggestion->scrollToElement(m_vSuggestionButtons[m_iSelectedSuggestion]);

				for (int i=0; i<m_vSuggestionButtons.size(); i++)
				{
					if (i == m_iSelectedSuggestion)
					{
						m_vSuggestionButtons[i]->setTextColor(0xff00ff00);
						m_vSuggestionButtons[i]->setTextDarkColor(0xff000000);
					}
					else
						m_vSuggestionButtons[i]->setTextColor(0xffffffff);
				}
			}

			e.consume();
		}
	} // handle command history up/down buttons
	else if (m_commandHistory.size() > 0 && m_textbox->isActive() && m_textbox->isVisible())
	{
		if (e == KEY_DOWN)
		{
			if (m_iSelectedHistory > m_commandHistory.size()-2)
				m_iSelectedHistory = 0;
			else
				m_iSelectedHistory++;

			if (m_iSelectedHistory > -1 && m_iSelectedHistory < m_commandHistory.size())
			{
				UString text = m_commandHistory[m_iSelectedHistory];
				m_textbox->setText(text);
				m_textbox->setCursorPosRight();
			}

			e.consume();
		}
		else if (e == KEY_UP)
		{
			if (m_iSelectedHistory < 1)
				m_iSelectedHistory = m_commandHistory.size() - 1;
			else
				m_iSelectedHistory--;

			if (m_iSelectedHistory > -1 && m_iSelectedHistory < m_commandHistory.size())
			{
				UString text = m_commandHistory[m_iSelectedHistory];
				m_textbox->setText(text);
				m_textbox->setCursorPosRight();
			}

			e.consume();
		}
	}
}

void ConsoleBox::onChar(KeyboardEvent &e)
{
	if (m_bConsoleAnimateOut && !m_bConsoleAnimateIn) return;

	m_textbox->onChar(e);

	if (m_textbox->isActive() && m_textbox->isVisible())
	{
		// rebuild suggestion list
		clearSuggestions();

		std::vector<ConVar*> suggestions = convar->getConVarByLetter(m_textbox->getText());
		for (int i=0; i<suggestions.size(); i++)
		{
			addSuggestion(suggestions[i]->getName());
		}
		m_suggestion->setVisible(suggestions.size() > 0);

		if (suggestions.size() > 0)
			m_suggestion->scrollToElement(m_suggestion->getContainer()->getElements()[0]);

		m_iSelectedSuggestion = -1;
	}
}

void ConsoleBox::onResolutionChange(Vector2 newResolution)
{
	const float dpiScale = getDPIScale();

	m_textbox->setSize(newResolution.x - 10 * dpiScale, m_textbox->getRelSize().y * dpiScale);
	m_textbox->setPos(5 * dpiScale, m_textbox->isVisible() ? newResolution.y - m_textbox->getSize().y - 6 * dpiScale : newResolution.y);

	m_suggestion->setPos(5 * dpiScale, newResolution.y - m_fSuggestionY);
	m_suggestion->setSizeX(newResolution.x - 10 * dpiScale);
}

void ConsoleBox::processCommand(UString command)
{
	clearSuggestions();
	m_iSelectedHistory = -1;

	if (command.length() > 0)
		m_commandHistory.push_back(command);

	Console::processCommand(command);
}

void ConsoleBox::execConfigFile(UString filename)
{
	Console::execConfigFile(filename);
}

bool ConsoleBox::isBusy()
{
	return (m_textbox->isBusy() || m_suggestion->isBusy()) && m_textbox->isVisible();
}

bool ConsoleBox::isActive()
{
	return (m_textbox->isActive() || m_suggestion->isActive()) && m_textbox->isVisible();
}

void ConsoleBox::addSuggestion(UString text)
{
	const float dpiScale = getDPIScale();

	const int vsize = m_vSuggestionButtons.size() + 1;
	const int bottomAdd = 3 * dpiScale;
	const int buttonheight = 22 * dpiScale;
	const int addheight = (17 + 8) * dpiScale;

	// create button and add it
	CBaseUIButton *button = new CBaseUIButton(3 * dpiScale, (vsize - 1)*buttonheight + 2 * dpiScale, 100, addheight, text, text);
	button->setDrawFrame(false);
	button->setSizeX(button->getFont()->getStringWidth(text));
	button->setClickCallback( fastdelegate::MakeDelegate(this, &ConsoleBox::onSuggestionClicked) );
	button->setDrawBackground(false);

	m_suggestion->getContainer()->addBaseUIElement(button);
	m_vSuggestionButtons.insert(m_vSuggestionButtons.begin(), button);

	// update suggestion size
	const int gap = 10 * dpiScale;
	m_fSuggestionY = clamp<float>(buttonheight * vsize, 0, buttonheight * 4) + (engine->getScreenHeight() - m_textbox->getPos().y) + gap;

	if (buttonheight * vsize > buttonheight * 4)
	{
		m_suggestion->setSizeY(buttonheight * 4 + bottomAdd);
		m_suggestion->setScrollSizeToContent();
	}
	else
	{
		m_suggestion->setSizeY(buttonheight * vsize + bottomAdd);
		m_suggestion->setScrollSizeToContent();
	}

	m_suggestion->setPosY(engine->getScreenHeight() - m_fSuggestionY);

	m_iSuggestionCount++;
}

void ConsoleBox::clearSuggestions()
{
	m_iSuggestionCount = 0;
	m_suggestion->getContainer()->clear();
	m_vSuggestionButtons = std::vector<CBaseUIButton*>();
	m_suggestion->setVisible(false);
}

void ConsoleBox::log(UString text)
{
#ifdef MCENGINE_FEATURE_MULTITHREADING

	std::lock_guard<std::mutex> lk(g_consoleBoxLogOverlayMutex);

#endif

	int newline = text.find("\n", 0);
	while (newline != -1)
	{
		text.erase(newline, 1);
		newline = text.find("\n", 0);
	}

	m_log.push_back(text);

	while (m_log.size() > console_overlay_lines.getInt())
	{
		m_log.erase(m_log.begin());
	}

	anim->deleteExistingAnimation(&m_fLogYPos);
	m_fLogYPos = 0;

	m_fLogTime = engine->getTime() + 8.0f;
}

float ConsoleBox::getAnimTargetY()
{
	return 32.0f * getDPIScale();
}

float ConsoleBox::getDPIScale()
{
	return ((float)std::max(env->getDPI(), m_textbox->getFont()->getDPI()) / 96.0f); // NOTE: abusing font dpi
}
