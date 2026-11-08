#include "pch.h"
#include "MainWindow.xaml.h"
#include "ServiceEditor.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::NssmNext::implementation {
void MainWindow::New_Click(Windows::Foundation::IInspectable const &, RoutedEventArgs const &) {
    ShowEditor(true);
}
void MainWindow::Edit_Click(Windows::Foundation::IInspectable const &, RoutedEventArgs const &) {
    ShowEditor(false);
}
void MainWindow::Delete_Click(Windows::Foundation::IInspectable const &, RoutedEventArgs const &) {
    Execute(Action::Remove);
}
void MainWindow::Start_Click(Windows::Foundation::IInspectable const &, RoutedEventArgs const &) {
    Execute(Action::Start);
}
void MainWindow::Stop_Click(Windows::Foundation::IInspectable const &, RoutedEventArgs const &) {
    Execute(Action::Stop);
}
void MainWindow::Restart_Click(Windows::Foundation::IInspectable const &, RoutedEventArgs const &) {
    Execute(Action::Restart);
}
void MainWindow::Elevate_Click(Windows::Foundation::IInspectable const &, RoutedEventArgs const &) {
    Elevate();
}

Windows::Foundation::IAsyncOperation<bool> MainWindow::EnsureWriteAccess() {
    const auto lifetime = get_strong();
    if (m_closed || m_busy || m_dialog_open)
        co_return false;
    if (m_manager->CanWrite())
        co_return true;
    ContentDialog permission;
    permission.XamlRoot(RootPanel().XamlRoot());
    permission.Title(box_value(L"需要管理员权限"));
    permission.Content(box_value(L"查看服务不需要提升权限。新增、编辑、删除和启停需要管理员权限。是"
                                 L"否重新以管理员身份打开？当前不会执行任何服务操作。"));
    permission.PrimaryButtonText(L"以管理员身份重新打开");
    permission.CloseButtonText(L"取消");
    permission.DefaultButton(ContentDialogButton::Close);
    m_dialog_open = true;
    UpdateControls();
    ContentDialogResult answer = ContentDialogResult::None;
    try {
        answer = co_await permission.ShowAsync();
    } catch (...) {
        m_dialog_open = false;
        UpdateControls();
        throw;
    }
    m_dialog_open = false;
    UpdateControls();
    if (m_closed || answer != ContentDialogResult::Primary)
        co_return false;
    if (nssm_next::ui::RelaunchElevated(m_owner))
        Application::Current().Exit();
    else
        StatusBar().Message(L"已取消权限提升，未修改任何服务。");
    co_return false;
}
fire_and_forget MainWindow::Elevate() {
    const auto lifetime = get_strong();
    try {
        co_await EnsureWriteAccess();
    } catch (nssm_next::ServiceError const &error) {
        ShowError(error.message);
    } catch (hresult_error const &error) {
        if (!m_closed)
            ShowError(std::wstring(error.message()));
    } catch (std::exception const &) {
        ShowError(L"请求管理员权限时发生异常。");
    }
}
fire_and_forget MainWindow::ShowEditor(bool isNew) {
    const auto lifetime = get_strong();
    if (m_closed || m_busy || m_dialog_open)
        co_return;
    try {
        if (!co_await EnsureWriteAccess())
            co_return;
        nssm_next::ServiceInfo expected;
        if (!isNew) {
            const auto selected = Selected();
            if (!selected)
                co_return;
            const auto manager = m_manager;
            apartment_context ui;
            SetBusy(true);
            std::wstring error;
            co_await resume_background();
            try {
                expected = manager->Read(*selected);
            } catch (nssm_next::ServiceError const &exception) {
                error = exception.message;
            } catch (std::exception const &) {
                error = L"读取服务配置时发生异常。";
            }
            co_await ui;
            if (m_closed)
                co_return;
            SetBusy(false);
            if (!error.empty()) {
                ShowError(error);
                co_return;
            }
        }
        const auto editor =
            nssm_next::ui::MakeServiceEditor(expected, isNew, RootPanel().XamlRoot(), m_owner,
                                             isNew ? L"LocalSystem" : expected.account);
        const auto weakWindow = get_weak();
        std::weak_ptr<nssm_next::ui::ServiceEditor> weakEditor = editor;
        editor->dialog.PrimaryButtonClick(
            [weakWindow, weakEditor, expected,
             isNew](auto const &,
                    ContentDialogButtonClickEventArgs const &eventArgs) -> fire_and_forget {
                const auto args = eventArgs;
                const auto deferral = args.GetDeferral();
                args.Cancel(true);
                const auto self = weakWindow.get();
                const auto state = weakEditor.lock();
                if (!self || !state || self->m_closed || self->m_busy) {
                    deferral.Complete();
                    co_return;
                }
                nssm_next::ServiceConfiguration configuration;
                try {
                    configuration = state->Configuration();
                    nssm_next::ValidateApplicationFiles(configuration);
                } catch (nssm_next::ServiceError const &error) {
                    state->ShowError(error.message);
                    deferral.Complete();
                    co_return;
                } catch (std::exception const &) {
                    state->ShowError(L"无法读取或验证输入。请检查配置。");
                    deferral.Complete();
                    co_return;
                }
                const auto manager = self->m_manager;
                apartment_context ui;
                self->SetBusy(true, true);
                state->dialog.IsEnabled(false);
                std::wstring error;
                nssm_next::ServiceInfo saved;
                co_await resume_background();
                try {
                    if (isNew)
                        saved = manager->Create(configuration);
                    else {
                        manager->Update(expected, configuration);
                        saved = expected;
                        static_cast<nssm_next::ServiceConfiguration &>(saved) = configuration;
                        ++saved.revision;
                    }
                } catch (nssm_next::ServiceError const &exception) {
                    error = exception.message;
                } catch (std::exception const &) {
                    error = L"保存服务时发生异常，请刷新列表检查实际状态。";
                }
                co_await ui;
                if (!self->m_closed) {
                    state->dialog.IsEnabled(true);
                    self->SetBusy(false);
                    if (error.empty()) {
                        state->saved = true;
                        state->saved_service = std::move(saved);
                        args.Cancel(false);
                    } else
                        state->ShowError(error);
                }
                deferral.Complete();
            });
        m_dialog_open = true;
        UpdateControls();
        co_await editor->dialog.ShowAsync();
        m_dialog_open = false;
        if (m_closed)
            co_return;
        if (editor->saved) {
            m_selected = editor->saved_service.name + L"\n" + editor->saved_service.instance_id;
            Refresh(isNew ? L"服务已创建，尚未启动。" : L"配置已保存；运行中的服务需重启后生效。");
        } else
            UpdateControls();
    } catch (nssm_next::ServiceError const &error) {
        if (!m_closed) {
            m_dialog_open = false;
            SetBusy(false);
            ShowError(error.message);
        }
    } catch (hresult_error const &error) {
        if (!m_closed) {
            m_dialog_open = false;
            SetBusy(false);
            ShowError(std::wstring(error.message()));
        }
    } catch (std::exception const &) {
        if (!m_closed) {
            m_dialog_open = false;
            SetBusy(false);
            ShowError(L"打开服务编辑器时发生异常。");
        }
    }
}
fire_and_forget MainWindow::Execute(Action action) {
    const auto lifetime = get_strong();
    if (m_closed || m_busy || m_dialog_open)
        co_return;
    try {
        const auto selected = Selected();
        if (!selected)
            co_return;
        if (!co_await EnsureWriteAccess())
            co_return;
        if (action == Action::Remove || action == Action::Restart) {
            ContentDialog confirmation;
            confirmation.XamlRoot(RootPanel().XamlRoot());
            confirmation.Title(
                box_value(action == Action::Remove ? L"删除 NssmNext 服务？" : L"重启服务？"));
            TextBlock description;
            description.TextWrapping(TextWrapping::Wrap);
            description.Text(action == Action::Remove
                                 ? L"将停止并删除服务注册：" + selected->display_name + L"（" +
                                       selected->name + L"）。应用程序、日志及宿主文件不会被删除。"
                                 : L"将停止再启动服务：" + selected->display_name +
                                       L"。运行中的任务可能会被中断。");
            confirmation.Content(description);
            confirmation.PrimaryButtonText(action == Action::Remove ? L"停止并删除" : L"重启服务");
            confirmation.CloseButtonText(L"取消");
            confirmation.DefaultButton(ContentDialogButton::Close);
            m_dialog_open = true;
            UpdateControls();
            const auto answer = co_await confirmation.ShowAsync();
            if (m_closed)
                co_return;
            if (answer != ContentDialogResult::Primary) {
                m_dialog_open = false;
                UpdateControls();
                co_return;
            }
            SetBusy(true, true);
            m_dialog_open = false;
        }
        const auto manager = m_manager;
        apartment_context ui;
        SetBusy(true, true);
        StatusBar().Severity(InfoBarSeverity::Informational);
        StatusBar().Message(L"正在执行服务操作，请稍候…");
        std::wstring error;
        co_await resume_background();
        try {
            switch (action) {
            case Action::Start:
                manager->Start(*selected);
                break;
            case Action::Stop:
                manager->Stop(*selected);
                break;
            case Action::Restart:
                manager->Stop(*selected);
                manager->Start(*selected);
                break;
            case Action::Remove:
                manager->Remove(*selected);
                break;
            }
        } catch (nssm_next::ServiceError const &exception) {
            error = exception.message;
        } catch (std::exception const &) {
            error = L"服务操作发生异常，请刷新检查实际状态。";
        }
        co_await ui;
        if (m_closed)
            co_return;
        SetBusy(false);
        if (!error.empty()) {
            ShowError(error);
            co_return;
        }
        if (action == Action::Remove)
            m_selected.clear();
        std::wstring completed;
        switch (action) {
        case Action::Start:
            completed = L"服务已启动。";
            break;
        case Action::Stop:
            completed = L"服务已停止。";
            break;
        case Action::Restart:
            completed = L"服务已重启。";
            break;
        case Action::Remove:
            completed = L"已提交服务注册删除；应用与日志文件保留。";
            break;
        }
        Refresh(completed);
    } catch (nssm_next::ServiceError const &error) {
        if (!m_closed) {
            m_dialog_open = false;
            SetBusy(false);
            ShowError(error.message);
        }
    } catch (hresult_error const &error) {
        if (!m_closed) {
            m_dialog_open = false;
            SetBusy(false);
            ShowError(std::wstring(error.message()));
        }
    } catch (std::exception const &) {
        if (!m_closed) {
            m_dialog_open = false;
            SetBusy(false);
            ShowError(L"服务操作发生异常。");
        }
    }
}
} // namespace winrt::NssmNext::implementation
