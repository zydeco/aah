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
    [self configureSearchBar];
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

- (void)configureSearchBar {
    self.searchBar.searchTextField.autocapitalizationType = UITextAutocapitalizationTypeNone;
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
    
    self.textView.text = @"Disassembling...";
    [self performSelectorInBackground:@selector(disassembleMethod:) withObject:@((uint64_t)addr)];
    results = @[methodName, @"", @""];
    return YES;
}

- (void)disassembleMethod:(NSNumber*)address {
    NSInteger armScore = 0;
    NSInteger x86Score = 0;
    void *addr = (void*)address.unsignedLongLongValue;
    results = @[
        results[0],
        [codeBrowser disassembleMethod:addr cpuType:CPU_TYPE_ARM64 score:&armScore],
        [codeBrowser disassembleMethod:addr cpuType:CPU_TYPE_X86_64 score:&x86Score]
    ];
    
    NSInteger index = (armScore > x86Score) ? 0 : 1;
    [self performSelectorOnMainThread:@selector(selectArchitecture:) withObject:@(index) waitUntilDone:NO];
}

- (void)selectArchitecture:(NSNumber*)index {
    if (index.integerValue == 0 && self.architecturePicker.selectedSegmentIndex != 0) {
        // Segment won't update when changed programmatically to 0 in catalyst
        [self.architecturePicker removeAllSegments];
        [self.architecturePicker insertSegmentWithTitle:@"arm64" atIndex:0 animated:NO];
        [self.architecturePicker insertSegmentWithTitle:@"x86_64" atIndex:1 animated:NO];
    }
    self.architecturePicker.selectedSegmentIndex = index.integerValue;
    [self.architecturePicker sendActionsForControlEvents:UIControlEventValueChanged];
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
