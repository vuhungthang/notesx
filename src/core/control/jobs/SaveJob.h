/*
 * Xournal++
 *
 * A job which saves a Document
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>  // for string

#include "BlockingJob.h"  // for BlockingJob
#include "filesystem.h"   // for path

class Control;


class SaveJob: public BlockingJob {
public:
    SaveJob(Control* control, std::function<void(bool)> = [](bool) {});

protected:
    ~SaveJob() override;

public:
    void run() override;

    bool save();

    static void updatePreview(Control* control);

protected:
    void afterRun() override;

private:
    std::string lastError;
    /// The file that was written, once the write really happened (Plan 004).
    fs::path savedFilepath;
    /// Called after saving, with boolean parameter true on success, false on failure (error)
    std::function<void(bool)> callback;
};
