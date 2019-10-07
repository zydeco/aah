//
//  AAHCodeBrowserViewController.h
//  TestApp
//
//  Created by Jesús A. Álvarez on 2019-10-06.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface AAHCodeBrowserViewController : UIViewController <UISearchBarDelegate>

@property (nonatomic, weak) IBOutlet UISearchBar *searchBar;
@property (nonatomic, weak) IBOutlet UITextView *textView;
@property (nonatomic, weak) IBOutlet UISegmentedControl *architecturePicker;

- (IBAction)changeArchitecture:(id)sender;
@end

NS_ASSUME_NONNULL_END
