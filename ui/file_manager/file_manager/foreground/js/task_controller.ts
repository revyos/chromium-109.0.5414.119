// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * This file is checked via TS, so we suppress Closure checks.
 * @suppress {checkTypes}
 */
import {assertInstanceof, assertNotReached} from 'chrome://resources/js/assert.js';

import {getMimeType, startIOTask} from '../../common/js/api.js';
import {DialogType} from '../../common/js/dialog_type.js';
import {metrics} from '../../common/js/metrics.js';
import {str, strf, util} from '../../common/js/util.js';
import {VolumeManagerCommon} from '../../common/js/volume_manager_types.js';
import {Crostini} from '../../externs/background/crostini.js';
import {ProgressCenter} from '../../externs/background/progress_center.js';
import {FilesAppDirEntry, FilesAppEntry} from '../../externs/files_app_entry_interfaces.js';
import {VolumeManager} from '../../externs/volume_manager.js';
import {FilesPasswordDialog} from '../elements/files_password_dialog.js';

import {DirectoryModel} from './directory_model.js';
import {FileSelection, FileSelectionHandler} from './file_selection.js';
import {AnnotatedTask, FileTasks, TaskPickerType} from './file_tasks.js';
import {FileTransferController} from './file_transfer_controller.js';
import {MetadataModel} from './metadata/metadata_model.js';
import {MetadataUpdateController} from './metadata_update_controller.js';
import {TaskHistory} from './task_history.js';
import {Command} from './ui/command.js';
import {FileManagerUI} from './ui/file_manager_ui.js';

/**
 * Type of the object stashed in the Map extractTasks_.
 */
interface ExtractingTasks {
  entries: Array<Entry|FilesAppEntry>;
  params: chrome.fileManagerPrivate.IOTaskParams;
}

export class TaskController {
  private fileTransferController_: FileTransferController|null = null;
  private taskHistory_: TaskHistory;
  private canExecuteDefaultTask_: boolean = false;
  private canExecuteOpenActions_: boolean = false;
  private defaultTaskCommand_: Command;
  /**
   * More actions command that uses #open-with as selector due to the
   * open-with command used previously for the same task.
   */
  private openWithCommand_: Command;
  /**
   * Cached promise used to avoid initializing the same FileTasks
   * multiple times.
   */
  private tasks_: Promise<FileTasks>|null = null;
  private tasksEntries_: Entry[];
  /** Map used to track extract IOTasks in progress.  */
  private extractTasks_: Map<number, ExtractingTasks> = new Map();
  /** Selected entries from the last time onSelectionChanged_ was called.  */
  private lastSelectedEntries_: Entry[];

  constructor(
      private dialogType_: DialogType, private volumeManager_: VolumeManager,
      private ui_: FileManagerUI, private metadataModel_: MetadataModel,
      private directoryModel_: DirectoryModel,
      private selectionHandler_: FileSelectionHandler,
      private metadataUpdateController_: MetadataUpdateController,
      private crostini_: Crostini, private progressCenter_: ProgressCenter) {
    this.taskHistory_ = new TaskHistory();
    this.defaultTaskCommand_ =
        assertInstanceof(document.querySelector('#default-task'), Command);
    this.openWithCommand_ =
        assertInstanceof(document.querySelector('#open-with'), Command);
    this.tasksEntries_ = [];
    this.lastSelectedEntries_ = [];

    ui_.taskMenuButton.addEventListener(
        'select', this.onTaskItemClicked_.bind(this));
    this.selectionHandler_.addEventListener(
        FileSelectionHandler.EventType.CHANGE,
        this.onSelectionChanged_.bind(this));
    this.selectionHandler_.addEventListener(
        FileSelectionHandler.EventType.CHANGE_THROTTLED,
        this.updateTasks_.bind(this));
    this.taskHistory_.addEventListener(
        TaskHistory.EventType.UPDATE, this.updateTasks_.bind(this));
    chrome.fileManagerPrivate.onIOTaskProgressStatus.addListener(
        this.onIOTaskProgressStatus_.bind(this));
    chrome.fileManagerPrivate.onAppsUpdated.addListener(
        this.clearCacheAndUpdateTasks_.bind(this));
  }

  setFileTransferController(fileTransferController: FileTransferController) {
    this.fileTransferController_ = fileTransferController;
  }

  /**
   * Task combobox handler.
   *
   * @param event Event containing task which was clicked.
   */
  private async onTaskItemClicked_(event: Event) {
    // If the clicked target has an associated command, the click event should
    // not be handled here since it is handled as a command.
    // TODO(lucmult): Add TS definition for these events instead of using any.
    if (event.target && (event.target as any).command) {
      return;
    }

    // 'select' event from ComboButton has the item as event.item.
    // 'activate' event from MenuButton has the item as event.target.data.
    const item: DropdownItem =
        (event as any).item || (event.target as any).data;
    try {
      const tasks = await this.getFileTasks();
      switch (item.type) {
        case TaskMenuItemType.SHOW_MENU:
          this.ui_.taskMenuButton.showMenu(false);
          break;
        case TaskMenuItemType.RUN_TASK:
          tasks.execute(item.task);
          break;
        case TaskMenuItemType.CHANGE_DEFAULT_TASK:
          const selection = this.selectionHandler_.selection;
          const extensions = [];

          for (let i = 0; i < selection.entries.length; i++) {
            const match = /\.(\w+)$/g.exec(selection.entries[i]!.toURL());
            if (match) {
              const ext = match[1]!.toUpperCase();
              if (extensions.indexOf(ext) == -1) {
                extensions.push(ext);
              }
            }
          }

          let format = '';

          if (extensions.length == 1) {
            format = extensions[0]!;
          }

          // Change default was clicked. We should open "change default" dialog.
          tasks.showTaskPicker(
              this.ui_.defaultTaskPicker, str('CHANGE_DEFAULT_MENU_ITEM'),
              strf('CHANGE_DEFAULT_CAPTION', format),
              this.changeDefaultTask_.bind(this, selection),
              TaskPickerType.ChangeDefault);
          break;
        default:
          assertNotReached('Unknown task.');
      }
    } catch (error: any) {
      if (error) {
        console.warn(error.stack || error);
      }
    }
  }

  /**
   * Sets the given task as default, when this task is applicable.
   *
   * @param selection File selection.
   * @param task Task to set as default.
   */
  private async changeDefaultTask_(
      selection: FileSelection, task: chrome.fileManagerPrivate.FileTask) {
    const entries = selection.entries;

    const mimeTypes =
        await Promise.all(entries.map(entry => this.getMimeType_(entry)));
    chrome.fileManagerPrivate.setDefaultTask(
        task.descriptor, entries, mimeTypes, util.checkAPIError);
    this.metadataUpdateController_.refreshCurrentDirectoryMetadata();

    // Update task menu button unless the task button was updated by other
    // selection.
    if (this.selectionHandler_.selection === selection) {
      this.tasks_ = null;
      try {
        const tasks = await this.getFileTasks();
        this.display_(tasks);
      } catch (error: any) {
        if (error) {
          console.warn(error.stack || error);
        }
      }
    }
    this.selectionHandler_.onFileSelectionChanged();
  }

  /** Displays the list of tasks in a open task picker combobutton. */
  private display_(fileTasks: FileTasks) {
    this.updateTasksDropdown_(fileTasks);
  }

  /**
   * Setup a task picker combobutton based on the given tasks. The combobutton
   * is not shown if there are no tasks, or if any entry is a directory.
   */
  private updateTasksDropdown_(fileTasks: FileTasks) {
    const combobutton = this.ui_.taskMenuButton;
    const tasks = fileTasks.getTaskItems();

    combobutton.hidden =
        tasks.length == 0 || fileTasks.entries.some(e => e.isDirectory);

    // If it's hidden, we don't have to update further the DOM.
    if (combobutton.hidden) {
      return;
    }

    combobutton.clear();

    const defaultTask = fileTasks.defaultTask;
    // If there exist defaultTask show it on the combobutton.
    if (defaultTask) {
      combobutton.defaultItem =
          createDropdownItem(defaultTask, str('TASK_OPEN'));
    } else {
      combobutton.defaultItem = {
        type: TaskMenuItemType.SHOW_MENU,
        label: str('OPEN_WITH_BUTTON_LABEL'),
      };
    }

    // If there exist 2 or more available tasks, show them in context menu
    // (including defaultTask). If only one generic task is available, we
    // also show it in the context menu.
    const items = this.createItems(fileTasks);
    if (items.length > 1 || (items.length === 1 && !defaultTask)) {
      for (const item of items) {
        combobutton.addDropDownItem(item);
      }

      // If there exist non generic task (i.e. defaultTask is set), we show
      // an item to change default task.
      if (defaultTask) {
        combobutton.addSeparator();
        const changeDefaultMenuItem = combobutton.addDropDownItem({
          type: TaskMenuItemType.CHANGE_DEFAULT_TASK,
          label: str('CHANGE_DEFAULT_MENU_ITEM'),
        });
        changeDefaultMenuItem.classList.add('change-default');

        // Disables CHANGE_DEFAULT button if default has been set by policy.
        if (fileTasks.getPolicyDefaultHandlerStatus()) {
          // |defaultTask| exists, thus |policyDefaultHandlerStatus| cannot be
          // INCORRECT_ASSIGNMENT.
          console.assert(
              fileTasks.getPolicyDefaultHandlerStatus() ===
              chrome.fileManagerPrivate.PolicyDefaultHandlerStatus
                  .DEFAULT_HANDLER_ASSIGNED_BY_POLICY);
          changeDefaultMenuItem.disabled = true;
        }
      }
    }
  }

  /**
   * Creates sorted array of available task descriptions such as title and icon.
   *
   * @param fileTasks File Tasks to create items.
   * @return Created array can be used to feed combobox, menus and so on.
   */
  createItems(fileTasks: FileTasks): DropdownItem[] {
    const tasks = fileTasks.getTaskItems();
    const items = [];

    // Create items.
    for (const task of tasks) {
      if (task === fileTasks.defaultTask) {
        const title = task.title + ' ' + str('DEFAULT_TASK_LABEL');
        items.push(createDropdownItem(task, title, true, true));
      } else {
        items.push(createDropdownItem(task));
      }
    }

    // Sort items (Sort order: isDefault, lastExecutedTime, label).
    items.sort((a, b) => {
      // Sort by isDefaultTask.
      const isDefault = (b.isDefault ? 1 : 0) - (a.isDefault ? 1 : 0);
      if (isDefault !== 0) {
        return isDefault;
      }

      // Sort by last-executed time.
      const aTime = this.taskHistory_.getLastExecutedTime(a.task.descriptor);
      const bTime = this.taskHistory_.getLastExecutedTime(b.task.descriptor);
      if (aTime !== bTime) {
        return bTime - aTime;
      }

      // Sort by label.
      return a.label.localeCompare(b.label);
    });

    return items;
  }


  /** Executes default task.  */
  async executeDefaultTask() {
    try {
      const tasks = await this.getFileTasks();
      const task: chrome.fileManagerPrivate.FileTask = {
        descriptor: this.ui_.defaultTaskMenuItem.descriptor!,
        title: this.ui_.defaultTaskMenuItem.label,
        get iconUrl() {
          console.assert(false);
          return '';
        },
        get isDefault() {
          console.assert(false);
          return false;
        },
        get isGenericFileHandler() {
          console.assert(false);
          return false;
        },
      };

      tasks.execute(task);
    } catch (error: any) {
      if (error) {
        console.warn(error.stack || error);
      }
    }
  }

  /**
   * Get MIME type for an entry. This method first tries to obtain the MIME type
   * from metadata. If it fails, this falls back to obtain the MIME type from
   * its content or name.
   * @param entry An entry to obtain its mime type.
   */
  private async getMimeType_(entry: Entry): Promise<string> {
    const properties =
        await this.metadataModel_.get([entry], ['contentMimeType']);
    if (properties && properties[0]!.contentMimeType) {
      return properties[0]!.contentMimeType;
    }
    const mimeType = await getMimeType(entry);
    return mimeType || '';
  }

  /** Handles change of selection and clears context menu.  */
  private onSelectionChanged_() {
    if (window.IN_TEST) {
      (this.ui_.taskMenuButton as unknown as HTMLElement)
          .removeAttribute('get-tasks-completed');
    }
    const selection = this.selectionHandler_.selection;
    // Caller of update context menu task items.
    // FileSelectionHandler.EventType.CHANGE
    if (this.dialogType_ === DialogType.FULL_PAGE &&
        (selection.directoryCount > 0 || selection.fileCount > 0)) {
      // Compare entries while ignoring changes inside directories.
      if (!util.isSameEntries(this.lastSelectedEntries_, selection.entries)) {
        // Update the context menu if selection changed.
        this.updateContextMenuTaskItems_(
            {tasks: [], policyDefaultHandlerStatus: undefined});
      }
    } else {
      // Update context menu.
      this.updateContextMenuTaskItems_(
          {tasks: [], policyDefaultHandlerStatus: undefined});
    }
    this.lastSelectedEntries_ = selection.entries;
  }

  /**
   * Explicitly removes the cached tasks first and and re-calculates the current
   * tasks.
   */
  private clearCacheAndUpdateTasks_() {
    this.tasks_ = null;
    this.updateTasks_();
  }

  /** Updates available tasks opened from context menu or the open button.  */
  private async updateTasks_() {
    const selection = this.selectionHandler_.selection;
    const shouldDisableTasks = (
        // File Picker/Save As doesn't show the "Open" button.
        this.dialogType_ !== DialogType.FULL_PAGE ||
        // The list of available tasks should not be available to trashed items.
        this.directoryModel_.getCurrentRootType() ==
            VolumeManagerCommon.RootType.TRASH ||
        // Nothing selected, so no "Open" button.
        selection.totalCount === 0);

    if (shouldDisableTasks) {
      this.ui_.taskMenuButton.hidden = true;
      if (window.IN_TEST) {
        this.ui_.taskMenuButton.toggleAttribute('get-tasks-completed', true);
      }
      return;
    }

    try {
      const metricName = 'UpdateAvailableApps';
      metrics.startInterval(metricName);
      const tasks = await this.getFileTasks();
      // Update the DOM.
      this.display_(tasks);
      const openTaskItems = tasks.getTaskItems();
      const policyDefaultHandlerStatus = tasks.getPolicyDefaultHandlerStatus();
      this.updateContextMenuTaskItems_(
          {tasks: openTaskItems, policyDefaultHandlerStatus});
      if (window.IN_TEST) {
        this.ui_.taskMenuButton.toggleAttribute('get-tasks-completed', true);
      }
      metrics.recordDirectoryListLoadWithTolerance(
          metricName, openTaskItems.length, [10, 100], /*tolerance=*/ 0.8);
    } catch (error: any) {
      if (error) {
        console.warn(error.stack || error);
      }
    }
  }

  async getFileTasks(): Promise<FileTasks> {
    const selection = this.selectionHandler_.selection;
    if (this.tasks_ &&
        util.isSameEntries(this.tasksEntries_, selection.entries)) {
      return this.tasks_;
    }
    this.tasksEntries_ = selection.entries;
    this.tasks_ = this.fetchTasks_();
    return this.tasks_;
  }

  /**
   * Fetch FileTasks, it should be only used by getFileTasks() because
   * getFileTasks() takes into account the caching.
   */
  private async fetchTasks_(): Promise<FileTasks> {
    const selection = this.selectionHandler_.selection;
    await selection.computeAdditional(this.metadataModel_);
    if (this.selectionHandler_.selection !== selection) {
      if (util.isSameEntries(this.tasksEntries_, selection.entries)) {
        this.tasks_ = null;
      }
      throw new Error('stale selection');
    }
    const tasks = await FileTasks.create(
        this.volumeManager_, this.metadataModel_, this.directoryModel_,
        this.ui_, this.fileTransferController_!, selection.entries,
        this.taskHistory_, this.crostini_, this.progressCenter_, this);

    if (this.selectionHandler_.selection !== selection) {
      if (util.isSameEntries(this.tasksEntries_, selection.entries)) {
        this.tasks_ = null;
      }
      throw new Error('stale selection');
    }
    return tasks;
  }

  /** Returns whether default task command can be executed or not. */
  canExecuteDefaultTask(): boolean {
    return this.canExecuteDefaultTask_;
  }

  /** Returns whether open with command can be executed or not. */
  canExecuteOpenActions(): boolean {
    return this.canExecuteOpenActions_;
  }

  /**
   * Updates tasks menu item to match passed task items.
   * @param openTasks List of OPEN tasks.
   */
  private updateContextMenuTaskItems_(
      openTasks: chrome.fileManagerPrivate.ResultingTasks) {
    const taskCount = openTasks.tasks.length;
    if (taskCount > 0) {
      const defaultTask =
          FileTasks.getDefaultTask(openTasks, this.taskHistory_);
      if (defaultTask) {
        const menuItem = this.ui_.defaultTaskMenuItem;
        /**
         * Menu icon can be controlled by either `iconEndImage` or
         * `iconEndFileType`, since the default task menu item DOM is shared,
         * before updating it, we should remove the previous one, e.g. reset
         * both `iconEndImage` and `iconEndFileType`.
         */
        menuItem.iconEndImage = '';
        menuItem.removeIconEndFileType();

        menuItem.setIconEndHidden(false);
        // iconType is defined for some tasks in FileTasks.annotate_().
        const iconType: string = (defaultTask as any).iconType;
        if (iconType) {
          menuItem.iconEndFileType = iconType;
        } else if (defaultTask.iconUrl) {
          menuItem.iconEndImage = 'url(' + defaultTask.iconUrl + ')';
        } else {
          menuItem.setIconEndHidden(true);
        }

        menuItem.label = defaultTask.title;
        menuItem.descriptor = defaultTask.descriptor;
      }

      this.canExecuteDefaultTask_ = defaultTask != null;
      this.defaultTaskCommand_.canExecuteChange(this.ui_.listContainer.element);
    }

    this.canExecuteOpenActions_ = taskCount > 1;
    this.openWithCommand_.canExecuteChange(this.ui_.listContainer.element);

    this.ui_.tasksSeparator.hidden = taskCount === 0;
  }

  /**
   * Return the tasks for the FileEntry |entry|.
   * @param entry
   */
  async getEntryFileTasks(entry: FileEntry): Promise<FileTasks> {
    return FileTasks.create(
        this.volumeManager_, this.metadataModel_, this.directoryModel_,
        this.ui_, this.fileTransferController_!, [entry], this.taskHistory_,
        this.crostini_, this.progressCenter_, this);
  }

  async executeEntryTask(entry: FileEntry) {
    const tasks = await this.getEntryFileTasks(entry);
    tasks.executeDefault();
  }

  /** Removes information about an extract archive task.  */
  private deleteExtractTaskDetails_(taskId: number) {
    this.extractTasks_.delete(taskId);
  }

  private onIOTaskProgressStatus_(
      event: chrome.fileManagerPrivate.ProgressStatus) {
    const taskId = event.taskId;
    if (!taskId) {
      console.warn('IOTask ProgressStatus without taskId');
      return;
    }

    // TaskController only manages IOTasks related to zip extract that were
    // started in this window.
    if (!(this.extractTasks_.has(taskId) &&
          event.type === chrome.fileManagerPrivate.IOTaskType.EXTRACT)) {
      return;
    }

    switch (event.state) {
      case chrome.fileManagerPrivate.IOTaskState.SUCCESS:
      case chrome.fileManagerPrivate.IOTaskState.CANCELLED:
      case chrome.fileManagerPrivate.IOTaskState.ERROR:
        this.deleteExtractTaskDetails_(taskId);
        break;
      case chrome.fileManagerPrivate.IOTaskState.NEED_PASSWORD:
        this.handleMissingPassword_(taskId);
        break;
    }
  }

  /**
   * Starts the Zip extract Here IO Task.
   * @param {!Array<!Entry|FilesAppEntry>} entries
   * @param {!DirectoryEntry|!FilesAppDirEntry} destination
   * @return {!Promise<void>} resolved with taskId.
   */
  async startExtractIOTask(
      entries: Array<Entry|FilesAppEntry>,
      destination: DirectoryEntry|FilesAppDirEntry): Promise<void> {
    const params = {
      destinationFolder: destination,
    } as chrome.fileManagerPrivate.IOTaskParams;
    return this.startExtractTask_(entries, params);
  }

  /**
   * Starts extraction for a single entry and stores the task details.
   * @return resolved with taskId.
   */
  private async startExtractTask_(
      entries: Array<Entry|FilesAppEntry>,
      params: chrome.fileManagerPrivate.IOTaskParams): Promise<void> {
    try {
      const taskId = await startIOTask(
          chrome.fileManagerPrivate.IOTaskType.EXTRACT, entries, params);
      this.extractTasks_.set(taskId, {entries, params});
    } catch (error: any) {
      console.warn('Error getting extract taskID', error);
    }
  }

  /**
   * Triggers a password dialog and starts an extract task with the
   * password (unless cancel is clicked on the dialog).
   */
  private async startGetPasswordThenExtractTask_(
      entry: Entry|FilesAppEntry,
      params: chrome.fileManagerPrivate.IOTaskParams) {
    let password: string|null = null;
    // Ask for password.
    try {
      const dialog = this.ui_.passwordDialog as FilesPasswordDialog;
      password = await dialog.askForPassword(entry.fullPath, password);
    } catch (error) {
      console.warn('User cancelled password fetch ', error);
      return;
    }

    params['password'] = password;
    await this.startExtractTask_([entry], params);
  }

  /**
   * If an extract operation has finished due to missing password,
   * see if we have the operation stored and if so, pop up a password
   * dialog and try to restart another IO operation for it.
   */
  private handleMissingPassword_(taskId: number) {
    const existingOperation = this.extractTasks_.get(taskId);
    if (existingOperation) {
      // If we have multiple entries (from a multi-select extract) then
      // we need to start a new task for each of them individually so
      // that the password dialog is presented once for every file
      // that's encrypted.
      const selectionEntries = existingOperation['entries'];
      const params = existingOperation['params'];
      if (selectionEntries.length == 1) {
        this.startGetPasswordThenExtractTask_(selectionEntries[0]!, params);
      } else {
        for (const entry of selectionEntries) {
          this.startExtractTask_([entry], params);
        }
      }
    }
    // Remove the failed operation reference since it's finished.
    this.deleteExtractTaskDetails_(taskId);
  }
}

/** Type of the task in the dropdown menu. */
enum TaskMenuItemType {
  SHOW_MENU = 'ShowMenu',
  RUN_TASK = 'RunTask',
  CHANGE_DEFAULT_TASK = 'ChangeDefaultTask',
}

/** Item in the dropdown menu. */
export interface DropdownItem {
  type: TaskMenuItemType;
  label: string;
  iconUrl?: string;
  iconType: string;
  task: chrome.fileManagerPrivate.FileTask;
  bold: boolean;
  isDefault: boolean;
  isGenericFileHandler?: boolean;
}

/**
 * Creates dropdown item based on task.
 * @param bold Make a menu item bold.
 * @param isDefault Mark the item as default item.
 */
function createDropdownItem(
    task: chrome.fileManagerPrivate.FileTask, title?: string, bold?: boolean,
    isDefault?: boolean): DropdownItem {
  return {
    type: TaskMenuItemType.RUN_TASK,
    label: title || task.title,
    iconUrl: task.iconUrl || '',
    iconType: (task as AnnotatedTask).iconType || '',
    task: task,
    bold: bold || false,
    isDefault: isDefault || false,
    isGenericFileHandler: task.isGenericFileHandler,
  };
}
