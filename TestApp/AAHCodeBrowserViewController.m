//
//  AAHCodeBrowserViewController.m
//  TestApp
//
//  Created by Jesús A. Álvarez on 2019-10-06.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import "AAHCodeBrowserViewController.h"
#import "AAHCodeBrowser.h"

@interface AAHCodeBrowserViewController () <UITableViewDelegate, UITableViewDataSource>

@end

@implementation AAHCodeBrowserViewController
{
    AAHCodeBrowser *codeBrowser;
    NSArray<NSString*> *bookmarks;
    NSArray<NSString*> *results;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    codeBrowser = [AAHCodeBrowser new];
    bookmarks = @[
        @"main",
        @"exit",
        @"-[AAPLAppDelegate init]",
        @"-[AAPLAppDelegate application:didFinishLaunchingWithOptions:]",
        @"+[UIColor purpleColor]",
        @"+[UIColor aapl_applicationPurpleColor]"
    ];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self browseMethod:self.searchBar.text];
}

- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
    if ([segue.identifier isEqualToString:@"bookmarks"]) {
        UITableViewController *destination = segue.destinationViewController;
        destination.tableView.delegate = self;
        destination.tableView.dataSource = self;
    }
}
#pragma mark - UISearchBarDelegate

- (void)searchBarSearchButtonClicked:(UISearchBar *)searchBar {
    if ([self browseMethod:searchBar.text]) {
        [searchBar resignFirstResponder];
    }
}

#pragma mark - Code Browser

- (BOOL)browseMethod:(NSString*)methodName {
    void *addr = [codeBrowser findMethodWithName:methodName];
    if (addr == NULL) {
        self.textView.text = [NSString stringWithFormat:@"%@: method not found.", methodName];
        return NO;
    }
    
    results = @[
        methodName,
        [codeBrowser disassembleMethod:addr cpuType:CPU_TYPE_ARM64],
        [codeBrowser disassembleMethod:addr cpuType:CPU_TYPE_X86_64]
    ];
    [self changeArchitecture:self];
    return YES;
}

- (void)changeArchitecture:(id)sender {
    self.textView.text = [NSString stringWithFormat:@"%@:\n%@", results[0], results[1+self.architecturePicker.selectedSegmentIndex]];
}

#pragma mark - Bookmarks

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    return @"Bookmarks";
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return bookmarks.count;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"bookmark"];
    cell.textLabel.text = bookmarks[indexPath.row];
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    NSString *methodName = bookmarks[indexPath.row];
    [self browseMethod:methodName];
    self.searchBar.text = methodName;
    [self dismissViewControllerAnimated:YES completion:nil];
}

@end
