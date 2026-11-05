#include "protocol.hpp"
#import <AppKit/AppKit.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import <Security/AuthSession.h>
#include <chrono>
#include <unistd.h>

@interface DraftDelegate : NSObject<NSSharingServiceDelegate>
@property BOOL done;
@property BOOL succeeded;
@property(strong) NSError* failure;
@end
@implementation DraftDelegate
- (void)sharingService:(NSSharingService*)service didShareItems:(NSArray*)items {
    self.succeeded=YES; self.done=YES;
}
- (void)sharingService:(NSSharingService*)service didFailToShareItems:(NSArray*)items error:(NSError*)error {
    self.failure=error; self.done=YES;
}
@end

namespace mail {
namespace {
NSString* text(const std::string& s) { return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding]; }
}
std::string capabilities() {
    return R"({"protocol":1,"backend":"ns-sharing-service","client":"apple-mail","fields":{"to":true,"cc":false,"bcc":false,"subject":true,"body":true,"attachments":true},"interactiveRequired":true,"timeoutSeconds":540,"deliveryConfirmed":false,"stagingCleanup":true})";
}
void available(const Request& r) {
    if(r.client!="apple-mail") throw Error("unsupported_client","this platform supports apple-mail only",3);
    @autoreleasepool {
        SecuritySessionId session=0;
        SessionAttributeBits attributes=0;
        if(SessionGetInfo(callerSecuritySession,&session,&attributes)!=errSecSuccess ||
           !(attributes&sessionHasGraphicAccess) || (attributes&sessionIsRemote))
            throw Error("interactive_session_required","a local graphical security session is required",3);
        uid_t uid=0; gid_t gid=0;
        CFStringRef user=SCDynamicStoreCopyConsoleUser(nullptr,&uid,&gid);
        bool console=user && uid==getuid() && CFStringCompare(user,CFSTR("loginwindow"),0)!=kCFCompareEqualTo;
        if(user) CFRelease(user);
        if(!console) throw Error("interactive_session_required","an active local graphical login session is required",3);
        @try {
            NSURL* url=[[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:@"com.apple.mail"];
            if(!url) throw Error("client_unavailable","Apple Mail is not installed",3);
        } @catch(NSException* exception) {
            throw Error("backend_unavailable","cannot inspect Apple Mail availability",3);
        }
    }
}
std::string compose(const Request& r,const Staging& staged) {
    @autoreleasepool {
        NSSharingService* service=nil;
        @try {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
            [NSApp finishLaunching];
            service=[NSSharingService sharingServiceNamed:NSSharingServiceNameComposeEmail];
            if(!service) throw Error("backend_unavailable","composeEmail sharing service unavailable",3);
            NSMutableArray* recipients=[NSMutableArray array];
            for(const auto& s:r.to) [recipients addObject:text(s)];
            service.recipients=recipients;
            service.subject=text(r.subject);
            NSMutableArray* items=[NSMutableArray arrayWithObject:text(r.body)];
            for(const auto& p:staged.paths) [items addObject:[NSURL fileURLWithPath:text(p)]];
            if(![service canPerformWithItems:items]) throw Error("backend_unavailable","composeEmail cannot handle this request",3);
            DraftDelegate* delegate=[DraftDelegate new];
            service.delegate=delegate;
            [service performWithItems:items];
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(540);
            while(!delegate.done && std::chrono::steady_clock::now()<deadline) {
                @autoreleasepool {
                    NSEvent* event=[NSApp nextEventMatchingMask:NSEventMaskAny
                        untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                        inMode:NSDefaultRunLoopMode dequeue:YES];
                    if(event) [NSApp sendEvent:event];
                }
            }
            service.delegate=nil;
            if(!delegate.done) throw Error("timeout","sharing callback not observed within 540 seconds; outcome unknown",4);
            if(delegate.succeeded) return "handed_off";
            if([delegate.failure.domain isEqualToString:NSCocoaErrorDomain] &&
               delegate.failure.code==NSUserCancelledError) return "cancelled";
            throw Error("backend_error","Apple Mail sharing failed; outcome not confirmed",3);
        } @catch(NSException* exception) {
            service.delegate=nil;
            throw Error("backend_error","AppKit sharing raised an exception",3);
        }
    }
}
}
