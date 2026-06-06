#ifndef SCROLLBEHAVIOR_H
#define SCROLLBEHAVIOR_H

class QApplication;

class ScrollBehavior {
public:
    static void install(QApplication& app);

private:
    ScrollBehavior() = delete;
    ~ScrollBehavior() = delete;
};

#endif // SCROLLBEHAVIOR_H
